#include "common/common.hpp"

#include "geodb/hilbert.hpp"

#include <boost/program_options.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include <iostream>
#include <limits>

namespace po = boost::program_options;

using namespace geodb;

using std::cout;
using std::cerr;

struct vec2 {
    double x = 0, y = 0;

    vec2() = default;
    vec2(double x, double y): x(x), y(y) {}
};

std::ostream& operator<<(std::ostream& o, const vec2& v) {
    return o << "(" << v.x << ", " << v.y << ")";
}

struct box2 {
    vec2 min, max;

    box2() = default;
    box2(vec2 min, vec2 max)
        : min(min), max(max)
    {
        geodb_assert(min.x <= max.x, "invalid x coordiantes");
        geodb_assert(min.y <= max.y, "invalid y coordiantes");
    }

    box2 extend(const vec2& point) const {
        return box2{
            vec2{std::min(min.x, point.x), std::min(min.y, point.y)},
            vec2{std::max(max.x, point.x), std::max(max.y, point.y)},
        };
    }

    double size() const {
        return (max.x - min.x) * (max.y - min.y);
    }
};

std::ostream& operator<<(std::ostream& o, const box2& b) {
    return o << "{" << b.min << ", " << b.max << "}";
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
                             "Visualizes the hilbert loading algorithm for a generated set of points in 2d.\n"
                             "The real algorithm works in 3d and is implemented in the bulk loader tool.\n"
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

/// Maps a point in [0, 1]^2 to its hilbert index.
u64 index(const vec2& p) {
    geodb_assert(p.x >= 0 && p.x <= 1, "invalid x coordinate");
    geodb_assert(p.y >= 0 && p.y <= 1, "invalid y coordinate");

    static constexpr u32 dimension = 2;
    static constexpr u32 precision = 8;
    static constexpr u32 max_coordiante = (1 << precision) - 1;

    using curve = hilbert_curve<dimension, precision>;

    auto map_coord = [](double value) {
        return curve::coordinate_t(u32(value * max_coordiante));
    };

    curve::point_t point{map_coord(p.x), map_coord(p.y)};
    return curve::hilbert_index(point);
}

std::vector<vec2> create_points(size_t count) {
    static std::mt19937 rng(std::random_device{}());

    std::uniform_real_distribution<double> dist(0, std::nextafter(1.0, std::numeric_limits<double>::max()));

    // Create uniformly distributed points in 0..1 x 0..1
    std::vector<vec2> points(count);
    for (auto& p : points) {
        p = vec2(dist(rng), dist(rng));
    }

    // Sort them by their hilbert value.
    std::sort(points.begin(), points.end(), [](const vec2& a, const vec2& b) {
        return index(a) < index(b);
    });
    return points;
}

std::vector<box2> create_leaves(const std::vector<vec2>& points, size_t leaf_size) {
    geodb_assert(leaf_size > 1, "invalid leaf size");

    // Computes the minimum bounding box for the given list of points.
    auto get_box = [](auto first, auto last) {
        geodb_assert(first != last, "cannot create the bounding box for an empty range");

        box2 box(*first, *first);
        ++first;
        for (; first != last; ++first) {
            box = box.extend(*first);
        }
        return box;
    };

    std::vector<box2> leaves;

    auto pos = points.begin();
    auto end = points.end();
    while (pos != end) {
        size_t count = std::min(leaf_size, size_t(end - pos));
        leaves.push_back(get_box(pos, pos + count));

        pos += count;
    }
    return leaves;
}


int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        std::vector<vec2> points = create_points(10000);
        std::vector<box2> leaves = create_leaves(points, 64);

        for (const auto& l : leaves) {
            fmt::print("{}\n", l);
        }

        // TODO Export as json.

        return 0;
    });
}
