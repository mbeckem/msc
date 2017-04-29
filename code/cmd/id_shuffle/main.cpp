#include "common/common.hpp"

#include "geodb/irwi/string_map.hpp"
#include "geodb/irwi/string_map_internal.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_internal.hpp"

#include <boost/program_options.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include <iostream>
#include <random>

using std::cout;
using std::cerr;

using namespace geodb;
namespace po = boost::program_options;

static std::string input_path;
static std::string output_path;

static void parse_options(int argc, char** argv) {
    po::options_description options("Options");
    options.add_options()
            ("help,h", "Show this message.")
            ("input", po::value(&input_path)->value_name("PATH")->required(),
             "The input file.")
            ("output", po::value(&output_path)->value_name("PATH")->required(),
             "The output file.");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0} OPTION...\n"
                             "\n"
                             "Takes an entry file and remaps the trajectory ids randomly.\n"
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

struct id_mapper {
public:
    id_mapper()
        : m_rng(std::random_device()())
        , m_dist(0, std::numeric_limits<trajectory_id_type>::max())
    {}

    void operator()(tree_entry& entry) {
        auto pos = m_ids.find(entry.trajectory_id);
        if (pos == m_ids.end()) {
            std::tie(pos, std::ignore) = m_ids.emplace(entry.trajectory_id,
                                                       generate_id());
        }
        entry.trajectory_id = pos->second;
    }

private:
    trajectory_id_type generate_id() {
        trajectory_id_type id;
        do {
            id = m_dist(m_rng);
        } while (m_ids.find(id) != m_ids.end());
        return id;
    }

private:
    std::unordered_map<trajectory_id_type, trajectory_id_type> m_ids;
    std::mt19937 m_rng;
    std::uniform_int_distribution<trajectory_id_type> m_dist;
};

int main(int argc, char *argv[]) {
    return tpie_main([&]() {
        parse_options(argc, argv);

        id_mapper map_id;

        tpie::file_stream<tree_entry> input;
        input.open(input_path, tpie::open::read_only);

        tpie::file_stream<tree_entry> output;
        output.open(output_path);
        output.truncate(0);

        while (input.can_read()) {
            tree_entry entry = input.read();
            map_id(entry);
            output.write(entry);
        }

        return 0;
    });
}
