#include "common/common.hpp"
#include "geodb/filesystem.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/bulk_load_str.hpp"
#include "geodb/irwi/bulk_load_quickload.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"

#include <boost/program_options.hpp>
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

using algorithm_type = std::function<void(external_tree&, tpie::file_stream<tree_entry>&, tpie::progress_indicator_base&)>;

static string algorithm;
static string trajectories_path;
static string tree_path;
static size_t memory;
static size_t max_leaves;
static float beta;

void parse_options(int argc, char** argv);

void create_entries(const string& path,
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

        tpie::progress_indicator_arrow arrow("Progress", 100);
        arrow.set_indicator_length(60);
        arrow.init();

        tpie::progress_indicator_subindicator create_progress(&arrow, 10, "Create leaf entries");
        create_entries(trajectories_path, entries, create_progress);

        tpie::progress_indicator_subindicator load_progress(&arrow, 90, "Create tree");

        const tpie::stream_size_type read_before = tpie::get_bytes_read();
        const tpie::stream_size_type written_before = tpie::get_bytes_written();
        loader(tree, entries, load_progress);

        const tpie::stream_size_type read_io = (tpie::get_bytes_read() - read_before) / block_size;
        const tpie::stream_size_type written_io = (tpie::get_bytes_written() - written_before) / block_size;

        arrow.done();

        fmt::print("\n"
                   "Read blocks: {}\n"
                   "Written blocks: {}\n", read_io, written_io);

        return 0;
    });
}

void parse_options(int argc, char** argv) {
    po::options_description options("Options");
    options.add_options()
            ("help,h", "Show this message.")
            ("algorithm", po::value(&algorithm)->value_name("ALG")->required(),
             "Uses the specified algorithm for bulk loading.\n"
             "Possible values are \"str\" and \"quickload\".")
            ("trajectories", po::value(&trajectories_path)->value_name("PATH")->required(),
             "Path to prepared trajectory file.")
            ("tree", po::value(&tree_path)->value_name("PATH")->required(),
             "Path to irwi tree directory. Will be created if it doesn't exist.")
            ("beta", po::value(&beta)->value_name("BETA")->default_value(0.5f),
             "Weight factor between 0 and 1 for spatial and textual cost (1.0 is a normal rtree).")
            ("max-memory", po::value(&memory)->value_name("MB")->default_value(32),
             "Memory limit in megabytes. Used by the str algorithm.")
            ("max-leaves", po::value(&max_leaves)->value_name("LEAVES")->default_value(8192),
             "Leaf limit. Used by the quickload algorithm.");

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

        po::notify(vm);
    } catch (const po::error& e) {
        fmt::print(cerr, "Failed to parse arguments: {}.\n", e.what());
        throw exit_main(1);
    }
}

algorithm_type get_algorithm() {
    if (algorithm == "str") {
        fmt::print(cout, "Using STR with memory limit {} MB.\n", memory);
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input, tpie::progress_indicator_base& progress) {
            size_t limit = memory * 1024 * 1024;
            tpie::get_memory_manager().set_limit(limit);
            bulk_load_str(tree, input, progress);
        };
    } else if (algorithm == "quickload") {
        fmt::print(cout, "Using Quickload with leaf limit {}.\n", max_leaves);
        return [&](external_tree& tree, tpie::file_stream<tree_entry>& input, tpie::progress_indicator_base& progress) {
            bulk_load_quickload(tree, input, max_leaves, progress);
        };
    } else {
        fmt::print(cerr, "Invalid algorithm: {}.\n", algorithm);
        throw exit_main(1);
    }
}

void create_entries(const string& path,
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
                    last = pos;
                }
            }
        }

        progress.step(trajectories.offset() - offset);
    }
    progress.done();
}
