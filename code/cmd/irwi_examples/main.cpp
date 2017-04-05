#include "common/common.hpp"

#include "geodb/irwi/string_map.hpp"
#include "geodb/irwi/string_map_internal.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_internal.hpp"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <iostream>

using std::cout;
using std::cerr;

using namespace geodb;

using internal_tree = tree<tree_internal<4>, 2>;

using internal_strings = string_map<string_map_internal>;

static std::vector<vector3> points_a{
    {10, 5, 0}, {4, 10, 5}, {15, 18, 20}, {30, 20, 30}
};

static std::vector<vector3> points_b{
    {19, 4, 0}, {16, 8, 5}, {16, 15, 10}, {12, 20, 15}, {9, 20, 20}
};

static std::vector<vector3> points_c{
    {10, 8, 10}, {13, 9, 15}, {13, 12, 20}, {14, 15, 25}, {15, 19, 30}
};

static std::vector<std::string> labels_a{
    "foot", "train", "train"
};

static std::vector<std::string> labels_b{
    "car", "car", "car", "car"
};

static std::vector<std::string> labels_c{
    "foot", "foot", "foot", "bike"
};

static std::vector<trajectory_unit> units(internal_strings& strings,
                                          const std::vector<vector3>& points,
                                          const std::vector<std::string>& labels)
{
    geodb_assert(points.size() >= 2, "need at least two points");
    geodb_assert(points.size() - 1 == labels.size(), "need a label for every segment");

    std::vector<trajectory_unit> units;

    vector3 start = points.front();
    for (size_t i = 1; i < points.size(); ++i) {
        const std::string& label = labels[i - 1];
        vector3 end = points[i];

        units.push_back(trajectory_unit(start, end, strings.label_id_or_insert(label)));
        start = end;
    }

    return units;
}

static void insert_as_trajectory(internal_tree& tree, trajectory_id_type tid, const std::vector<trajectory_unit>& units) {
    u32 index = 0;
    for (const auto& unit : units) {
        tree_entry entry(tid, index++, unit);
        tree.insert(entry);
    }
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    return tpie_main([&]() {
        internal_strings strings;
        const auto units_a = units(strings, points_a, labels_a);
        const auto units_b = units(strings, points_b, labels_b);
        const auto units_c = units(strings, points_c, labels_c);

        auto print_units = [](const auto& units) {
            u32 index = 0;
            for (const auto& unit : units) {
                fmt::print("{:5}: {}\n", index++, unit);
            }
        };

        fmt::print("Trajectory 1:\n");
        print_units(units_a);
        fmt::print("\n");

        fmt::print("Trajectory 2:\n");
        print_units(units_b);
        fmt::print("\n");

        fmt::print("Trajectory 3:\n");
        print_units(units_c);
        fmt::print("\n");

        fmt::print("Strings mapping:\n");
        for (const auto& mapping : strings) {
            fmt::print("{:5}: {}\n", mapping.id, mapping.name);
        }
        fmt::print("\n");

        internal_tree tree({}, 1.0);
        insert_as_trajectory(tree, 1, units_a);
        insert_as_trajectory(tree, 2, units_b);
        insert_as_trajectory(tree, 3, units_c);

        fmt::print("Tree:\n");
        dump(cout, tree.root(), 2);

        return 0;
    });
}
