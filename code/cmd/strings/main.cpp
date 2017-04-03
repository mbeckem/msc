#include "common/common.hpp"

#include "geodb/klee.hpp"

#include <boost/program_options.hpp>
#include <fmt/ostream.h>

#include <iostream>
#include <string>
#include <sstream>

using namespace std;
using namespace geodb;
namespace po = boost::program_options;

static std::string strings;

static void parse_options(int argc, char** argv) {
    po::options_description options;
    options.add_options()
            ("help,h", "Show this message.")
            ("strings", po::value(&strings)->value_name("PATH")->required(),
             "Path to the strings database.");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0} OPTION...\n"
                             "\n"
                             "Displays the content of a strings database.\n"
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

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        external_string_map string_map({strings});

        fmt::print(cout, "Content of {}:\n", strings);
        for (const auto& mapping : string_map) {
            fmt::print("\t{:8}: {}\n", mapping.id, mapping.name);
        }

        return 0;
    });
}

