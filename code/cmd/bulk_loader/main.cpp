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
static string tree_path;
static size_t memory;
static size_t max_leaves;
static float beta;
static string stats_file;
static boost::optional<u64> limit;

void parse_options(int argc, char** argv);

void create_entries(const string& path, u64 max_entries,
                    tpie::file_stream<tree_entry>& entries,
                    tpie::progress_indicator_base& progress);

algorithm_type get_algorithm();

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        fmt::print(cout, "Opening tree at {} with beta {}.\n", tree_path, beta);

        external_tree tree{external_storage(tree_path), beta};

        auto loader = get_algorithm();

        if (!fs::exists(trajectories_path)) {
            fmt::print(cerr, "File does not exist: {}\n", trajectories_path);
            return 1;
        }

        tpie::temp_file tmp;
        tpie::file_stream<tree_entry> entries;
        entries.open(tmp);
        entries.truncate(0);

        if (limit) {
            fmt::print("Limiting to {} entries.\n", *limit);
        }
        const u64 max_entries = limit.get_value_or(std::numeric_limits<u64>::max());

        const measure_t stats = measure_call([&]{
            tpie::progress_indicator_arrow create_progress("Creating leaf entries", 100);
            create_progress.set_indicator_length(60);
            create_entries(trajectories_path, max_entries, entries, create_progress);
            create_progress.done();

            fmt::print(cout, "Loading the tree structure...\n");
            loader(tree, entries);
            fmt::print(cout, "Done.\n");
        });

        fmt::print("\n"
                   "Blocks read: {}\n"
                   "Blocks written: {}\n"
                   "Seconds: {}\n",
                   stats.reads, stats.writes, stats.duration);

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
             "Possible values are \"str\", \"hilbert\" and \"quickload\".")
            ("trajectories", po::value(&trajectories_path)->value_name("PATH")->required(),
             "Path to prepared trajectory file.")
            ("tree", po::value(&tree_path)->value_name("PATH")->required(),
             "Path to irwi tree directory. Will be created if it doesn't exist.")
            ("beta", po::value(&beta)->value_name("BETA")->default_value(0.5f),
             "Weight factor between 0 and 1 for spatial and textual cost (1.0 is a normal rtree).")
            ("max-memory", po::value(&memory)->value_name("MB")->default_value(32),
             "Memory limit in megabytes. Used by the str and hilbert algorithms.")
            ("max-leaves", po::value(&max_leaves)->value_name("LEAVES")->default_value(8192),
             "Leaf limit. Used by the quickload algorithm.")
            ("stats", po::value(&stats_file)->value_name("FILE"),
             "Output path for stats in json format.")
            ("limit,n", po::value<u64>()->value_name("N"),
             "Only insert the first N entries.");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0} OPTION...\n"
                             "\n"
                             "Bulk load a tree from a list of trajectories.\n"
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
        fmt::print(cout, "Using STR with memory limit {} MB.\n", memory);
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            size_t limit = memory * 1024 * 1024;
            tpie::get_memory_manager().set_limit(limit);
            bulk_load_str(tree, input);
        };
    } else if (algorithm == "hilbert") {
        fmt::print(cout, "Using hilbert loading with memory limit {} MB.\n", memory);
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            size_t limit = memory * 1024 * 1024;
            tpie::get_memory_manager().set_limit(limit);
            bulk_load_hilbert(tree, input);
        };
    } else if (algorithm == "quickload") {
        fmt::print(cout, "Using Quickload with leaf limit {}.\n", max_leaves);
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input) {
            bulk_load_quickload(tree, input, max_leaves);
        };
    } else {
        fmt::print(cerr, "Invalid algorithm: {}.\n", algorithm);
        throw exit_main(1);
    }
}

void create_entries(const string& path, u64 max_entries,
                    tpie::file_stream<tree_entry>& entries,
                    tpie::progress_indicator_base& progress)
{
    tpie::serialization_reader trajectories;
    trajectories.open(path);

    const tpie::stream_size_type bytes = trajectories.size();

    // Steps are measured in bytes.
    progress.init(bytes);
    point_trajectory trajectory;
    while (trajectories.can_read()) {
        auto offset = trajectories.offset();
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
                        goto done;
                    }

                    last = pos;
                }
            }
        }

        progress.step(trajectories.offset() - offset);
    }
done:
    progress.done();
}
