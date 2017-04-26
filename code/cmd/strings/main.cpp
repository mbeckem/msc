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

static std::string input;
static bool json_format = false;

static void parse_options(int argc, char** argv) {
    po::options_description options;
    options.add_options()
            ("help,h", "Show this message.")
            ("input", po::value(&input)->value_name("PATH")->required(),
             "Path to the strings database.")
            ("json", po::bool_switch(&json_format),
             "Enable json output format.");

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

        external_string_map string_map({input});

        if (json_format) {
            json result = json::object();
            for (const auto& mapping : string_map) {
                result[mapping.name] = mapping.id;
            }

            fmt::print("{}\n", result.dump(4));
            return 0;
        }

        fmt::print(cout, "Content of {}:\n", input);
        for (const auto& mapping : string_map) {
            fmt::print("\t{:8}: {}\n", mapping.id, mapping.name);
        }

        return 0;
    });
}

