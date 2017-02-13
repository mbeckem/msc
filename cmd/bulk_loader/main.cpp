#include "geodb/filesystem.hpp"
#include "geodb/tpie_main.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/str_loader.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"

#include <boost/program_options.hpp>
#include <fmt/ostream.h>
#include <tpie/tpie.h>
#include <tpie/fractional_progress.h>
#include <tpie/progress_indicator_arrow.h>
#include <tpie/serialization_stream.h>

#include <iostream>
#include <string>
#include <vector>

using namespace std;
using namespace geodb;
namespace po = boost::program_options;

static string trajectories_path;
static string tree_path;

void parse_options(int argc, char** argv);

void create_entries(tpie::serialization_reader& trajectories,
                    tpie::uncompressed_stream<tree_entry>& entries,
                    tpie::progress_indicator_base& progress);

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        // TODO: Parameter!
        tpie::get_memory_manager().set_limit(32 * 1024 * 1024);

        if (!fs::exists(trajectories_path)) {
            fmt::print(cerr, "File does not exist: {}\n", trajectories_path);
            return 1;
        }

        tree<tree_external<4096>, 40> tree(tree_external<4096>{tree_path});

        tpie::serialization_reader reader;
        reader.open(trajectories_path);

        tpie::temp_file tmp;
        tpie::uncompressed_stream<tree_entry> entries;
        entries.open(tmp);
        entries.truncate(0);

        tpie::progress_indicator_arrow arrow("Progress", 100);
        arrow.set_indicator_length(60);
        arrow.init();

        tpie::progress_indicator_subindicator create_progress(&arrow, 10, "Create leaf entries");
        create_entries(reader, entries, create_progress);

        tpie::progress_indicator_subindicator str_progress(&arrow, 90, "STR");
        bulk_load_str(tree, entries, str_progress);

        arrow.done();

        return 0;
    });
}

void parse_options(int argc, char** argv) {
    po::options_description options("Options");
    options.add_options()
            ("help,h", "Show this message.")
            ("trajectories", po::value(&trajectories_path)->value_name("PATH")->required(),
             "Path to prepared trajectory file.")
            ("tree", po::value(&tree_path)->value_name("PATH")->required(),
             "Path to irwi tree directory. Will be created if it doesn't exist.");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0} OPTION...\n"
                             "\n"
                             "Bulk load an IRWI tree using the STR algorithm"
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

void create_entries(tpie::serialization_reader& trajectories,
                    tpie::uncompressed_stream<tree_entry>& entries,
                    tpie::progress_indicator_base& progress)
{
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
                    tree_entry e{trajectory.id, index++, trajectory_unit{last->spatial, pos->spatial, last->textual}};
                    entries.write(e);
                    last = pos;
                }
            }
        }

        progress.step(trajectories.offset() - offset);
    }
    progress.done();
}
