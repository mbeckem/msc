#include "common/common.hpp"

#include "geodb/hilbert.hpp"
#include "geodb/str.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_internal.hpp"
#include "geodb/irwi/bulk_load_quickload.hpp"

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

static std::string algorithm;
static u32 seed;
static u32 num_points;
static bool skewed;
static bool heuristic;

static constexpr size_t leaf_size = 16;

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
            ("skewed,s", po::bool_switch(&skewed)->default_value(false),
             "If false, generates uniformly distributed points in [0, 1]x[0, 1}. "
             "If true, the point set will be skewed instead.")
            ("seed", po::value(&seed),
             "The seed used by the random number generator. Defaults to a truly random seed.")
            ("algorithm", po::value(&algorithm),
             "The leaf packing algorithm. Either \"hilbert\", \"str\", \"quickload\" or \"obo\".")
            ("heuristic", po::bool_switch(&heuristic),
             "Enable the leaf size heuristic (hilbert only).")
            ("help,h", "Show this message.");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0}\n"
                             "\n"
                             "Visualizes the bulk loading algorithm for a generated set of points in 2d.\n"
                             "The real algorithm works in 3d and is implemented in the loader tool.\n"
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

/// Sorts the points by their hilbert index.
void sort_hilbert(std::vector<vec2>& points) {
    std::sort(points.begin(), points.end(), [](const vec2& a, const vec2& b) {
        return index(a) < index(b);
    });
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
    vec2 center_a(0.3, 0.7);
    vec2 center_b(0.5, 0.2);
    vec2 center_c(0.7, 0.5);

    double radius_a = 0.30;
    double radius_b = 0.15;
    double radius_c = 0.20;

    std::vector<vec2> points(count);

    // A point on the circle determined by a random radius
    // and a random arc.
    // Thus, the density of points will be hightest at the center
    // and will become lower with increasing distance.
    auto gen_circle = [&](const vec2& center, double radius) {
        double v = get_random() * 2 * M_PI;
        double r = get_random() * radius;

        vec2 p;
        p.x = r * std::cos(v) + center.x;
        p.y = r * std::sin(v) + center.y;
        return p;
    };

    for (auto& p : points) {
        double which = get_random();
        if (which < 0.5) {
            p = gen_circle(center_a, radius_a);
        } else if (which < 0.75) {
            p = gen_circle(center_b, radius_b);
        } else {
            p = gen_circle(center_c, radius_c);
        }
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


std::vector<leaf> create_hilbert_leaves(std::vector<vec2>& points) {
    geodb_assert(leaf_size > 1, "invalid leaf size");

    sort_hilbert(points);

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

std::vector<leaf> create_hilbert_leaves_heuristic(std::vector<vec2>& points) {
    geodb_assert(leaf_size > 1, "invalid leaf size");

    sort_hilbert(points);

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
        pos += count;

        // Take points while the box does not grow too large.
        double max_size = l.mbb.size() * max_grow;
        while (pos != end && l.points.size() < leaf_size) {
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

std::vector<leaf> create_str_leaves(std::vector<vec2>& points) {
    geodb_assert(leaf_size > 1, "invalid leaf size");

    auto cmp_x = [](const vec2& a, const vec2& b) {
        return a.x < b.x;
    };
    auto cmp_y = [](const vec2& a, const vec2& b) {
        return a.y < b.y;
    };
    sort_tile_recursive(points, leaf_size, cmp_x, cmp_y);

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

struct vec2_accessor {
    trajectory_id_type get_id(const vec2& e) const {
        (void) e;
        return 0;
    }

    bounding_box get_mbb(const vec2& e) const {
        return bounding_box(vector3(e.x, e.y, 0),
                            vector3(e.x, e.y, 1));
    }

    constexpr u64 get_total_count(const vec2& e) const {
        (void) e;
        return 1;
    }

    std::array<label_count, 1> get_label_counts(const vec2& e) const {
        (void) e;
        return { label_count(0, 1) };
    }
};

std::vector<leaf> create_quickload_leaves(const std::vector<vec2>& points) {
    geodb_assert(leaf_size > 1, "invalid leaf size");

    // quickload implementation currently requires a file stream.
    tpie::file_stream<vec2> entries;
    entries.open();
    for (const auto& point : points) {
        entries.write(point);
    }

    std::vector<leaf> leaves;

    // 16 leaves in memory.
    quick_load_pass<vec2, vec2_accessor,
            block_size, leaf_size, leaf_size
    > pass(16, 16, vec2_accessor(), 1.0);
    pass.run(entries, [&](gsl::span<const vec2> entries) {
        leaf l;
        l.mbb = get_bounding_box(entries.begin(), entries.end());
        l.points.assign(entries.begin(), entries.end());
        leaves.push_back(std::move(l));
    });

    return leaves;
}

std::vector<leaf> create_obo_leaves(const std::vector<vec2>& points) {
    geodb_assert(leaf_size > 1, "invalid leaf size");

    using internal = geodb::tree_internal<leaf_size, leaf_size>;
    using tree_t = geodb::tree<internal, 1>;

    tree_t tree(internal(), 1.0);
    for (const auto& p : points) {
        tree_entry e;
        e.trajectory_id = 1;
        e.unit_index = 1;
        e.unit.label = 1;
        e.unit.start = vector3(p.x, p.y, 0);
        e.unit.end = vector3(p.x, p.y, 1);
        tree.insert(e);
    }

    struct leaf_visitor {
        std::vector<leaf> leaves;

        void operator()(tree_t::cursor& c) {
            if (c.is_leaf()) {
                leaf l;
                for (size_t i = 0; i < c.size(); ++i) {
                    tree_entry e = c.value(i);
                    l.points.push_back({e.unit.start.x(), e.unit.start.y()});
                }
                l.mbb = get_bounding_box(l.points.begin(), l.points.end());
                leaves.push_back(std::move(l));
            } else {
                for (size_t i = 0; i < c.size(); ++i) {
                    c.move_child(i);
                    (*this)(c);
                    c.move_parent();
                }
            }
        }
    } visitor;
    auto cursor = tree.root();
    visitor(cursor);
    return std::move(visitor.leaves);
}

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        // Generate a set of random points.
        std::vector<vec2> points = skewed
                ? create_skewed_points(num_points)
                : create_points(num_points);

        // Iterate over the sorted points and create leaf nodes.
        std::vector<leaf> leaves = [&] {
            if (algorithm == "hilbert") {
                return heuristic
                        ? create_hilbert_leaves_heuristic(points)
                        : create_hilbert_leaves(points);
            } else if (algorithm == "str") {
                return create_str_leaves(points);
            } else if (algorithm == "quickload") {
                return create_quickload_leaves(points);
            } else if (algorithm == "obo") {
                return create_obo_leaves(points);
            } else {
                fmt::print(cerr, "Invalid algorithm: {}.\n", algorithm);
                throw exit_main(1);
            }
        }();

        json result = {
            {"leaves", leaves},
            {"seed", seed},
        };

        cout << result.dump(4) << endl;
        return 0;
    });
}
