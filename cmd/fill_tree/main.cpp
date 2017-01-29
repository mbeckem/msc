#include "geodb/filesystem.hpp"
#include "geodb/tpie_main.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"

#include <boost/program_options.hpp>
#include <fmt/ostream.h>
#include <tpie/tpie.h>
#include <tpie/serialization_stream.h>

#include <iostream>
#include <string>
#include <vector>

using namespace std;
using namespace geodb;
namespace po = boost::program_options;

string trajectories_path;
string tree_path;
double beta;

using namespace geodb;

void parse_options(int argc, char** argv);

int main(int argc, char** argv) {
    using tree_storage_t = tree_external<block_size>;
    using tree_t = tree<tree_storage_t, 40>;

    return tpie_main([&]{
        parse_options(argc, argv);

        if (!fs::exists(trajectories_path)) {
            fmt::print(cerr, "File does not exist: {}\n", trajectories_path);
            return 1;
        }

        tpie::serialization_reader reader;
        reader.open(trajectories_path);

        std::vector<point_trajectory> data;
        reader.unserialize(data);

        tree_t tree(tree_storage_t{tree_path});

        fmt::print(cout, "Adding {} trajectories\n", data.size());
        fmt::print(cout, "Beta={}\n", beta);

        size_t size = data.size();
        for (size_t index = 0; index < size; ++index) {
            tree.insert(data[index]);
            fmt::print(cout, "{} of {} complete.\n", index + 1, size);

        }
        fmt::print("Done.\n");

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
