#include "geodb/filesystem.hpp"
#include "geodb/tpie_main.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"

#include <boost/program_options.hpp>
#include <fmt/ostream.h>
#include <tpie/tpie.h>
#include <tpie/progress_indicator_arrow.h>
#include <tpie/progress_indicator_base.h>
#include <tpie/serialization_stream.h>

#include <iostream>
#include <string>
#include <vector>

using namespace std;
using namespace geodb;
namespace po = boost::program_options;

static string trajectories_path;
static string tree_path;
static double beta;

using namespace geodb;

using tree_storage_t = tree_external<block_size>;
using tree_t = tree<tree_storage_t, 40>;

void insert_trajectories(tree_t& tree, tpie::serialization_reader& input, tpie::progress_indicator_base& progress);
void parse_options(int argc, char** argv);

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        if (!fs::exists(trajectories_path)) {
            fmt::print(cerr, "File does not exist: {}\n", trajectories_path);
            return 1;
        }

        tree_t tree(tree_storage_t{tree_path}, beta);
        fmt::print(cout, "Using tree \"{}\" of size {} with beta {}\n", tree_path, tree.size(), beta);

        tpie::serialization_reader reader;
        reader.open(trajectories_path);
        fmt::print(cout, "Size of input file {}: {} bytes\n", trajectories_path, reader.file_size());

        tpie::progress_indicator_arrow arrow("Inserting trajectories", 0);
        arrow.set_indicator_length(60);

        insert_trajectories(tree, reader, arrow);
        return 0;
    });
}

void insert_trajectories(tree_t& tree, tpie::serialization_reader& input, tpie::progress_indicator_base& progress)
{
    int count = 0;

    point_trajectory trajectory;

    progress.init(input.size());
    while (input.can_read()) {
        auto begin = input.offset();

        input.unserialize(trajectory);
        ++count;

        std::string title = fmt::format("Trajectory {}", count);
        progress.push_breadcrumb(title.c_str(), tpie::IMPORTANCE_MAJOR);
        progress.refresh();

        tree.insert(trajectory);

        progress.pop_breadcrumb();
        progress.step(input.offset() - begin);
    }
    progress.done();
}

void parse_options(int argc, char** argv) {
    po::options_description options("Options");
    options.add_options()
            ("help,h", "Show this message.")
            ("trajectories", po::value(&trajectories_path)->value_name("PATH")->required(),
             "Path to prepared trajectory file.")
            ("tree", po::value(&tree_path)->value_name("PATH")->required(),
             "Path to irwi tree directory. Will be created if it doesn't exist.")
            ("beta", po::value(&beta)->value_name("BETA")->default_value(0.5),
             "Weight factor between 0 and 1 for spatial and textual cost (1.0 is a normal rtree).");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0} OPTION...\n"
                             "\n"
                             "Fill an IRWI-Tree with trajectory data. The output file\n"
                             "will be created if it doesn't exist.\n"
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

    if (beta < 0 || beta > 1) {
        fmt::print(cerr, "Beta must be between 0 and 1: {}.\n", beta);
        throw exit_main(1);
    }
}
