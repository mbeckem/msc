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
static string entries_path;
static string tree_path;
static size_t memory;
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

        fmt::print("Memory limited to ca. {} MB.\n", memory);
        tpie::get_memory_manager().set_limit(memory * 1024 * 1024);

        if (!tmp.empty()) {
            fmt::print(cout, "Using tmp dir {}.\n", tmp);
            tpie::tempname::set_default_path(tmp);
        }

        fmt::print(cout, "Opening tree at \"{}\" with beta {}.\n", tree_path, beta);

        external_tree tree{external_storage(tree_path), beta};
        fmt::print(cout, "Inserting items into a tree of size {}.\n", tree.size());

        auto loader = get_algorithm();

        if (limit) {
            fmt::print("Limiting to {} entries.\n", *limit);
        }
        const u64 max_entries = limit.get_value_or(std::numeric_limits<u64>::max());

        tpie::file_stream<tree_entry> entries;
        {
            fmt::print(cout, "Using entry file \"{}\".\n", entries_path);

            // Make a private copy of the file (some options are destructive, i.e. STR sorting
            // alters the order of elements).
            tpie::file_stream<tree_entry> existing;
            existing.open(entries_path, tpie::open::read_only);

            entries.open();
            entries.truncate(0);
            while (entries.size() < max_entries && existing.can_read()) {
                entries.write(existing.read());
            }
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
             "  str-plain  \tTile entries using the Sort-Tile-Recursive algorithm and pack them into leaf nodes.\n"
             "  str-lf     \tTile like in str-plain, but sort by label as the first dimension.\n"
             "  str-ll     \tTile like in str-plain, but sort by label as the last dimension.\n"
             "  quickload  \tUse the quickload algorithm to pack entries into nodes on every level of the tree.\n")
            ("entries", po::value(&entries_path)->value_name("PATH")->required(),
             "Path to a file that already contains leaf entries.")
            ("tree", po::value(&tree_path)->value_name("PATH")->required(),
             "Path to irwi tree directory. Will be created if it doesn't exist.")
            ("beta", po::value(&beta)->value_name("BETA")->default_value(0.5f),
             "Weight factor between 0 and 1 for spatial and textual cost (1.0 is a normal rtree).")
            ("max-memory", po::value(&memory)->value_name("MB")->default_value(32),
             "Memory limit in megabytes.")
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
                             "Load a tree from a list of tree entries.\n"
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
    if (algorithm == "str-lf") {
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            using loader_t = str_loader<external_tree>;
            loader_t loader(tree, loader_t::sort_mode::label_first);
            loader.load(input);
        };
    } else if (algorithm == "str-plain") {
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            using loader_t = str_loader<external_tree>;
            loader_t loader(tree, loader_t::sort_mode::label_ignored);
            loader.load(input);
        };
    } else if (algorithm == "str-ll") {
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            using loader_t = str_loader<external_tree>;
            loader_t loader(tree, loader_t::sort_mode::label_last);
            loader.load(input);
        };
    } else if (algorithm == "hilbert") {
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            hilbert_loader<external_tree> loader(tree);
            loader.load(input);
        };
    } else if (algorithm == "quickload") {
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            // TODO: Adjust cache size.
            quick_loader<external_tree> loader(tree, 4);
            loader.load(input);
        };
    } else if (algorithm == "obo") {
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
