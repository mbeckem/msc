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
using std::endl;
using std::flush;

static u32 seed;
static u32 num_points;
static u32 leaf_size;
static bool skewed;
static bool heuristic;

struct vec2 {
    double x = 0, y = 0;

    vec2() = default;
    vec2(double x, double y): x(x), y(y) {}
};

void to_json(json& j, const vec2& vec) {
    j = {
        {"x", vec.x},
        {"y", vec.y}
    };
}

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

void to_json(json&j , const box2& box) {
    j = {
        {"min", box.min},
        {"max", box.max},
    };
}

struct leaf {
    box2 mbb;
    std::vector<vec2> points;
};

void to_json(json& j, const leaf& l) {
    j = {
        {"mbb", l.mbb},
        {"points", l.points},
    };
}

std::ostream& operator<<(std::ostream& o, const box2& b) {
    return o << "{" << b.min << ", " << b.max << "}";
}

void parse_options(int argc, char** argv) {
    po::options_description options("Options");
    options.add_options()
            ("points,p", po::value(&num_points)->default_value(1000),
             "The number of generated points.")
            ("leaf-size,l", po::value(&leaf_size)->default_value(64),
             "The number of entries per leaf.")
            ("heuristic", po::bool_switch(&heuristic),
             "Enable the leaf size heuristic.")
            ("skewed,s", po::bool_switch(&skewed)->default_value(false),
             "If false, generates uniformly distributed points in [0, 1]x[0, 1}. "
             "If true, the point set will be skewed instead.")
            ("seed", po::value(&seed),
             "The seed used by the random number generator. Defaults to a truly random seed.")
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

        if (!vm.count("seed")) {
            seed = std::random_device{}();
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
    static constexpr u32 precision = 16;
    static constexpr u32 max_coordiante = (1 << precision) - 1;

    using curve = hilbert_curve<dimension, precision>;

    auto map_coord = [](double value) {
        return curve::coordinate_t(u32(value * max_coordiante));
    };

    curve::point_t point{map_coord(p.x), map_coord(p.y)};
    return curve::hilbert_index(point);
}

/// Returns a random value in [0, 1].
double get_random() {
    static std::mt19937 rng(seed);
    static std::uniform_real_distribution<double> dist(0, std::nextafter(1.0, std::numeric_limits<double>::max()));
    return dist(rng);
}

/// Generates uniformly distributed points in [0, 1]^2.
std::vector<vec2> create_points(size_t count) {
    std::vector<vec2> points(count);
    for (auto& p : points) {
        p = vec2(get_random(), get_random());
    }
    return points;
}

/// Generates a skewed set of points in [0, 1]^2.
std::vector<vec2> create_skewed_points(size_t count) {
    std::vector<vec2> points(count);

    for (auto& p : points) {
        double x = get_random();
        double y = get_random();

        double w = 0.5 - std::fabs(0.5 - y);
        x = x * w + 0.5 - w / 2;
        x += std::fabs(y - 0.5);

        p = vec2(x, y);
    }
    return points;
}

/// Get the bounding box for a set of points.
/// There must be at least one point.
template<typename Iter>
box2 get_bounding_box(Iter first, Iter last) {
    geodb_assert(first != last, "cannot create the bounding box for an empty range");

    box2 box(*first, *first);
    ++first;
    for (; first != last; ++first) {
        box = box.extend(*first);
    }
    return box;
}

std::vector<leaf> create_leaves(const std::vector<vec2>& points, size_t leaf_size) {
    geodb_assert(leaf_size > 1, "invalid leaf size");

    std::vector<leaf> leaves;

    auto pos = points.begin();
    auto end = points.end();
    while (pos != end) {
        size_t count = std::min(leaf_size, size_t(end - pos));

        leaf l;
        l.mbb = get_bounding_box(pos, pos + count);
        l.points.assign(pos, pos + count);
        leaves.push_back(std::move(l));

        pos += count;
    }
    return leaves;
}

std::vector<leaf> create_leaf_heuristic(const std::vector<vec2>& points, size_t leaf_size) {
    geodb_assert(leaf_size > 1, "invalid leaf size");

    static constexpr double max_grow = 1.2;

    std::vector<leaf> leaves;

    auto pos = points.begin();
    auto end = points.end();
    while (pos != end) {
        size_t count = std::min(leaf_size / 2, size_t(end - pos));

        leaf l;

        // Take the first "count" points.
        l.points.assign(pos, pos + count);
        l.mbb = get_bounding_box(l.points.begin(), l.points.end());

        // Take points while the box does not grow too large.
        const double max_size = l.mbb.size() * max_grow;
        while (pos != end) {
            const vec2& p = *pos;
            box2 new_mbb = l.mbb.extend(p);
            if (new_mbb.size() > max_size) {
                break;
            }

            l.points.push_back(p);
            l.mbb = new_mbb;
            ++pos;
        }

        leaves.push_back(std::move(l));
    }

    return leaves;
}


int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        // Generate a set of random points.
        std::vector<vec2> points = skewed
                ? create_skewed_points(num_points)
                : create_points(num_points);

        // Sort them by their hilbert value.
        std::sort(points.begin(), points.end(), [](const vec2& a, const vec2& b) {
            return index(a) < index(b);
        });

        // Iterate over the sorted points and create leaf nodes.
        std::vector<leaf> leaves = heuristic
                ? create_leaf_heuristic(points, leaf_size)
                : create_leaves(points, leaf_size);

        json result = {
            {"leaves", leaves},
            {"seed", seed},
        };

        cout << result.dump(4) << endl;
        return 0;
    });
}
