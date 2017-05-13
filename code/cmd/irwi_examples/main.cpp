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

// Bike -> Train
static std::vector<vector3> points_1{
    {30, 20, 0}, {15, 18, 5}, {4, 10, 10}, {5, 5, 15}
};

// Car
static std::vector<vector3> points_2{
    {20, 5, 0}, {25, 10, 5}, {25, 20, 10}, {15, 25, 15}, {5, 25, 20}
};

// Pedestrian
static std::vector<vector3> points_3{
    {15, 9, 0}, {13, 12, 5}, {13, 15, 10}, {10, 18, 15}, {6, 17, 20}
};

static std::vector<std::string> labels_1{
    "train", "train", "bike"
};

static std::vector<std::string> labels_2{
    "car", "car", "car", "car"
};

static std::vector<std::string> labels_3{
    "car", "walk", "walk", "walk"
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

template<typename Tree>
static void insert_as_trajectory(Tree& tree, trajectory_id_type tid, const std::vector<trajectory_unit>& units) {
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
        const auto units_1 = units(strings, points_1, labels_1);
        const auto units_2 = units(strings, points_2, labels_2);
        const auto units_3 = units(strings, points_3, labels_3);

        auto print_units = [](const auto& units) {
            u32 index = 0;
            for (const auto& unit : units) {
                fmt::print("{:5}: {}\n", index++, unit);
            }
        };

        fmt::print("Trajectory 1:\n");
        print_units(units_1);
        fmt::print("\n");

        fmt::print("Trajectory 2:\n");
        print_units(units_2);
        fmt::print("\n");

        fmt::print("Trajectory 3:\n");
        print_units(units_3);
        fmt::print("\n");

        fmt::print("Strings mapping:\n");
        for (const auto& mapping : strings) {
            fmt::print("{:5}: {}\n", mapping.id, mapping.name);
        }
        fmt::print("\n");

        internal_tree tree({}, 1.0);
        insert_as_trajectory(tree, 1, units_1);
        insert_as_trajectory(tree, 2, units_2);
        insert_as_trajectory(tree, 3, units_3);

        fmt::print("Tree:\n");
        dump(cout, tree.root(), 2);
        return 0;
    });
}
