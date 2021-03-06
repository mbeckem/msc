#include "geolife.hpp"

#include "common/common.hpp"
#include "geodb/filesystem.hpp"
#include "geodb/parser.hpp"
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
static string log_path;

void parse_options(int argc, char** argv);

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);
        external_string_map label_map{string_map_external(labels)};

        fmt::print("Parsing geolife trajectories from {}\n", input);
        fmt::print("Writing results to {}\n", output);
        fmt::print("Labels file {}\n", labels);

        tpie::file_stream<tree_entry> out;
        out.open(output);
        out.truncate(0);

        tpie::progress_indicator_arrow arrow("Parsing dataset", 100);
        arrow.set_indicator_length(60);

        if (!log_path.empty()) {
            std::ofstream logger;
            logger.open(log_path);
            parse_geolife(input, label_map, out, logger, arrow);
        } else {
            std::ostream null_logger(nullptr);
            parse_geolife(input, label_map, out, null_logger, arrow);
        }
        return 0;
    });
}

void parse_options(int argc, char** argv) {
    po::options_description options;
    options.add_options()
            ("help,h", "Show this message.")
            ("data", po::value(&input)->value_name("PATH")->required(),
             "Path to the geolife dataset.")
            ("output,o", po::value(&output)->value_name("PATH")->required(),
             "Output file for tree entries.")
            ("log", po::value(&log_path)->value_name("PATH"),
             "File for logging trajectory mappings.")
            ("strings,s", po::value(&labels)->value_name("PATH")->required(),
             "String database on disk (for activity labels).");

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


