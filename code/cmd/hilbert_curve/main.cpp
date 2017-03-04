#include "common/common.hpp"

#include "geodb/hilbert.hpp"

#include <boost/program_options.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include <iostream>

namespace po = boost::program_options;

using std::cout;
using std::cerr;

void parse_options(int argc, char** argv);

template<u32 Dimension, u32 Precision>
json curve_json();

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        json curves = json::array({
            curve_json<2, 1>(),
            curve_json<2, 2>(),
            curve_json<2, 3>(),
            curve_json<3, 1>(),
            curve_json<3, 2>(),
            curve_json<3, 3>(),
        });

        fmt::print(cout, "{}\n", curves.dump(4));
        return 0;
    });
}

void parse_options(int argc, char** argv) {
    po::options_description options("Options");
    options.add_options()
            ("help,h", "Show this message.");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0}\n"
                             "\n"
                             "Outputs hilbert curve points for certain dimensions and precisions.\n"
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

/// Creates a vector of points that represents a walk over
/// the hilbert curve with the given Dimension and Precision.
template<u32 Dimension, u32 Precision>
auto curve_points()
{
    using curve_t = geodb::hilbert_curve<Dimension, Precision>;
    using point_t = typename curve_t::point_t;
    using index_t = typename curve_t::index_t;

    const index_t count = curve_t::index_count;

    std::vector<point_t> result(count);
    for (index_t i = 0; i < count; ++i) {
        result[i] = curve_t::hilbert_index_inverse(i);
    }
    return result;
}

template<u32 Dimension, u32 Precision>
json curve_json() {
    const auto points = curve_points<Dimension, Precision>();

    json json_data = json::object();
    json_data["dimension"] = Dimension;
    json_data["precision"] = Precision;

    json& json_points = json_data["points"] = json::array();
    for (const auto& point : points) {
        json jp = json::array();
        for (const auto& coord : point) {
            jp.push_back(coord.to_ullong());
        }
        json_points.push_back(jp);
    }
    return json_data;
}

