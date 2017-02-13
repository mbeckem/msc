#include "geolife.hpp"

#include "geodb/filesystem.hpp"
#include "geodb/parser.hpp"
#include "geodb/tpie_main.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/string_map.hpp"
#include "geodb/irwi/string_map_external.hpp"

#include <boost/program_options.hpp>
#include <fmt/ostream.h>
#include <tpie/tpie.h>
#include <tpie/progress_indicator_arrow.h>
#include <tpie/serialization_stream.h>

#include <iostream>
#include <string>

using namespace std;
using namespace geodb;
namespace po = boost::program_options;

static string input;
static string output;
static string labels;

void parse_options(int argc, char** argv);

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);
        labels_map label_map{string_map_external(labels)};

        tpie::serialization_writer out;
        out.open(output);

        fmt::print("Parsing geolife trajectories from {}\n", input);
        fmt::print("Writing results to {}\n", output);
        fmt::print("Labels file {}\n", labels);

        tpie::progress_indicator_arrow arrow("Parsing dataset", 100);
        arrow.set_indicator_length(60);

        parse_geolife(input, label_map, out, arrow);
        return 0;
    });
}

void parse_options(int argc, char** argv) {
    po::options_description options;
    options.add_options()
            ("help,h", "Show this message.")
            ("input", po::value(&input)->value_name("PATH")->required(),
             "Path to raw trajectory data.")
            ("output-trajectories", po::value(&output)->value_name("PATH")->required(),
             "Output file for trajectories.")
            ("output-labels", po::value(&labels)->value_name("PATH")->required());

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0} OPTION...\n"
                             "\n"
                             "Prepare trajectory and label data from an external source\n"
                             "Only supports geolife-style data for now.\n"
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


