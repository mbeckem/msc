#include "common/common.hpp"
#include "geodb/filesystem.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/bulk_load_hilbert.hpp"
#include "geodb/irwi/bulk_load_str.hpp"
#include "geodb/irwi/bulk_load_quickload.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"

#include <boost/program_options.hpp>
#include <boost/optional.hpp>
#include <fmt/ostream.h>
#include <tpie/tpie.h>
#include <tpie/fractional_progress.h>
#include <tpie/progress_indicator_arrow.h>
#include <tpie/serialization_stream.h>
#include <tpie/stats.h>

#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace std;
using namespace geodb;
namespace po = boost::program_options;

using algorithm_type = std::function<void(external_tree&, tpie::file_stream<tree_entry>&)>;

static string algorithm;
static string trajectories_path;
static string entries_path;
static string tree_path;
static size_t memory;
static size_t max_leaves;
static float beta;
static string stats_file;
static boost::optional<u64> limit;
static std::string tmp;

void parse_options(int argc, char** argv);

void create_entries(const string& path, u64 max_entries,
                    tpie::file_stream<tree_entry>& entries);

algorithm_type get_algorithm();

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        if (!tmp.empty()) {
            fmt::print(cout, "Using tmp dir {}.\n", tmp);
            tpie::tempname::set_default_path(tmp);
        }

        fmt::print(cout, "Opening tree at {} with beta {}.\n", tree_path, beta);

        external_tree tree{external_storage(tree_path), beta};
        fmt::print(cout, "Inserting items into a tree of size {}.\n", tree.size());

        auto loader = get_algorithm();

        if (limit) {
            fmt::print("Limiting to {} entries.\n", *limit);
        }
        const u64 max_entries = limit.get_value_or(std::numeric_limits<u64>::max());

        tpie::file_stream<tree_entry> entries;
        if (!trajectories_path.empty()) {
            fmt::print(cout, "Creating leaf entries from trajectory file \"{}\".\n", trajectories_path);

            entries.open();
            entries.truncate(0);

            create_entries(trajectories_path, max_entries, entries);
        } else if (!entries_path.empty()) {
            fmt::print(cout, "Using existing entry file \"{}\".\n", entries_path);

            // Make a private copy of the file (some options are destructive, i.e. STR sorting
            // alters the order of elements).
            tpie::file_stream<tree_entry> existing;
            existing.open(entries_path, tpie::open::read_only);

            entries.open();
            entries.truncate(0);
            while (entries.size() < max_entries && existing.can_read()) {
                entries.write(existing.read());
            }
        } else {
            fmt::print(cerr, "No input file specified.\n");
            throw exit_main(1);
        }

        const measure_t stats = measure_call([&]{
            fmt::print(cout, "Running {}.\n", algorithm);
            loader(tree, entries);
            fmt::print(cout, "Done.\n");
        });

        fmt::print("\n"
                   "Blocks read: {}\n"
                   "Blocks written: {}\n"
                   "Seconds: {}\n",
                   stats.read_io, stats.write_io, stats.duration);

        if (!stats_file.empty()) {
            write_json(stats_file, stats);
        }
        return 0;
    });
}

void parse_options(int argc, char** argv) {
    po::options_description options("Options");
    options.add_options()
            ("help,h", "Show this message.")
            ("algorithm", po::value(&algorithm)->value_name("ALG")->required(),
             "Uses the specified algorithm for bulk loading.\n"
             "Possible algorithm choices are:\n"
             "  obo        \tOne by one insertion (constant resource usage, very slow).\n"
             "  hilbert    \tSort entries by hilbert values and pack them into leaf nodes.\n"
             "  str        \tTile entries using the Sort-Tile-Recursive algorithm and pack them into leaf nodes.\n"
             "  str2       \tTile like in str, but sort by label last.\n"
             "  quickload  \tUse the quickload algorithm to pack entries into nodes on every level of the tree.\n")
            ("trajectories", po::value(&trajectories_path)->value_name("PATH"),
             "Path to prepared trajectory file.")
            ("entries", po::value(&entries_path)->value_name("PATH"),
             "Path to a file that already contains leaf entries.")
            ("tree", po::value(&tree_path)->value_name("PATH")->required(),
             "Path to irwi tree directory. Will be created if it doesn't exist.")
            ("beta", po::value(&beta)->value_name("BETA")->default_value(0.5f),
             "Weight factor between 0 and 1 for spatial and textual cost (1.0 is a normal rtree).")
            ("max-memory", po::value(&memory)->value_name("MB")->default_value(32),
             "Memory limit in megabytes. Used by the str and hilbert algorithms.")
            ("max-leaves", po::value(&max_leaves)->value_name("N")->default_value(8192),
             "Leaf limit. Used by the quickload algorithm.")
            ("stats", po::value(&stats_file)->value_name("FILE"),
             "Output path for stats in json format.")
            ("limit,n", po::value<u64>()->value_name("N"),
             "Only insert the first N entries.")
            ("tmp", po::value(&tmp)->value_name("PATH"),
             "Override the default temp directory.");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0} OPTION...\n"
                             "\n"
                             "Load a tree from a list of trajectories.\n"
                             "The algorithm and the resource limits can be chosen below.\n"
                             "\n"
                             "{1}",
                       argv[0], options);
            throw exit_main(0);
        }

        if (vm.count("limit")) {
            limit = vm["limit"].as<u64>();
        }

        po::notify(vm);
    } catch (const po::error& e) {
        fmt::print(cerr, "Failed to parse arguments: {}.\n", e.what());
        throw exit_main(1);
    }
}

algorithm_type get_algorithm() {
    if (algorithm == "str") {
        fmt::print(cout, "Using str loading with memory limit {} MB.\n", memory);
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            size_t limit = memory * 1024 * 1024;
            tpie::get_memory_manager().set_limit(limit);

            using loader_t = str_loader<external_tree>;
            loader_t loader(tree, loader_t::sort_mode::label_first);
            loader.load(input);
        };
    } else if (algorithm == "str2") {
        fmt::print(cout, "Using str2 loading with memory limit {} MB.\n", memory);
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            size_t limit = memory * 1024 * 1024;
            tpie::get_memory_manager().set_limit(limit);

            using loader_t = str_loader<external_tree>;
            loader_t loader(tree, loader_t::sort_mode::label_last);
            loader.load(input);
        };
    } else if (algorithm == "hilbert") {
        fmt::print(cout, "Using hilbert loading with memory limit {} MB.\n", memory);
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            size_t limit = memory * 1024 * 1024;
            tpie::get_memory_manager().set_limit(limit);

            hilbert_loader<external_tree> loader(tree);
            loader.load(input);
        };
    } else if (algorithm == "quickload") {
        fmt::print(cout, "Using quickload with leaf limit {}.\n", max_leaves);
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            quick_loader<external_tree> loader(tree, max_leaves);
            loader.load(input);
        };
    } else if (algorithm == "obo") {
        fmt::print(cout, "Using one-by-one insertion.\n");
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            tpie::progress_indicator_arrow progress("Inserting", 100);
            progress.set_indicator_length(60);

            progress.init(input.size());

            input.seek(0);
            while (input.can_read()) {
                tree.insert(input.read());
                progress.step();
            }
            progress.done();
        };
    } else {
        fmt::print(cerr, "Invalid algorithm: {}.\n", algorithm);
        throw exit_main(1);
    }
}

void create_entries(const string& path, u64 max_entries,
                    tpie::file_stream<tree_entry>& entries)
{
    tpie::serialization_reader trajectories;
    trajectories.open(path);

    point_trajectory trajectory;
    while (trajectories.can_read()) {
        trajectories.unserialize(trajectory);

        {
            auto pos = trajectory.entries.begin();
            auto end = trajectory.entries.end();
            u32 index = 0;

            if (pos != end) {
                auto last = pos++;
                for (; pos != end; ++pos) {
                    tree_entry e;
                    e.trajectory_id = trajectory.id;
                    e.unit_index = index++;
                    e.unit = trajectory_unit{last->spatial, pos->spatial, last->textual};

                    entries.write(e);
                    if (entries.size() >= max_entries) {
                        return;
                    }

                    last = pos;
                }
            }
        }
    }
}
