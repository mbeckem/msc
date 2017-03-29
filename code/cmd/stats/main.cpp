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

struct tree_stats {
    bounding_box mbb;

    double entry_area = 0.0;
    u64 entries = 0.0;

    double leaf_area = 0.0;
    u64 leaves = 0;

    double internal_union_area_ratio = 0.0;
    u64 internals = 0;
};

static void parse_options(int argc, char** argv) {
    po::options_description options;
    options.add_options()
            ("help,h", "Show this message.")
            ("input", po::value(&input)->value_name("PATH")->required(),
             "Path to the tree directory on disk.");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0} OPTION...\n"
                             "\n"
                             "Analyses the given tree and prints statistics.\n"
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

template<typename T>
std::string to_string(const T& t) {
    std::stringstream ss;
    ss << t;
    return ss.str();
}

/// Returns the path of the given node, as a string.
//static std::string path(const external_tree::cursor& node) {
//    std::string result;
//    for (auto id : node.path()) {
//        if (!result.empty())
//            result += "/";
//        result += std::to_string(id);
//    }
//    return result;
//}

/// a / b except when b is very close to 0 in which case it returns 0.
double div0(double a, double b) {
    if (std::fabs(b) < std::numeric_limits<double>::epsilon())
        return 0;
    return a / b;
}

/// Returns the bounding boxes of the given internal node's entries
/// as a vector of rect3d.
static std::vector<rect3d> get_rectangles(external_tree::cursor& node) {
    geodb_assert(node.is_internal(), "must be an internal node");
    geodb_assert(node.size() > 0, "nodes cannot be empty");

    const size_t size = node.size();

    std::vector<rect3d> rects;
    rects.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        const bounding_box mbb = node.mbb(i);
        rect3d rect(vector3d(mbb.min().x(), mbb.min().y(), mbb.min().t()),
                    vector3d(mbb.max().x(), mbb.max().y(), mbb.max().t()));

        rects.push_back(rect);
    }
    return rects;
}

static double volume_ratio(external_tree::cursor& node) {
    const std::vector<rect3d> rects = get_rectangles(node);

    double sum = 0.0;
    double max = 0.0;
    double usum = union_area(rects);

    for (const rect3d& r : rects) {
        const double vol = r.size();

        sum += vol;
        max = std::max(max, vol);
    }

    geodb_assert(usum >= max && usum <= sum,
                 "union volume must be in range");

    usum -= max;
    sum -= max;

    // 1.   Sum of volumes equals maximum volume, thus
    //      usum must be 0 or very small too.
    // 2.   Default case.
    return sum <= std::numeric_limits<double>::epsilon()
            ? 1 : usum / sum;
}

static void analyze(external_tree::cursor& node, tree_stats& stats) {
    if (node.is_leaf()) {
        for (size_t i = 0; i < node.size(); ++i) {
            stats.entry_area += node.mbb(i).size();
            stats.entries += 1;
        }

        stats.leaf_area += node.mbb().size();
        stats.leaves += 1;
    } else {
        stats.internal_union_area_ratio += volume_ratio(node);
        stats.internals += 1;

        for (size_t i = 0; i < node.size(); ++i) {
            node.move_child(i);
            analyze(node, stats);
            node.move_parent();
        }
    }
}

static tree_stats analyze(const external_tree& tree) {
    tree_stats stats;

    if (!tree.empty()) {
        auto root = tree.root();
        stats.mbb = root.mbb();
        analyze(root, stats);

        stats.entry_area /= stats.mbb.size();
        stats.leaf_area /= stats.mbb.size();
    }

    return stats;
}

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        external_tree tree{external_storage(input)};

        tree_stats stats = analyze(tree);
        if (stats.leaves != tree.leaf_node_count()) {
            throw std::logic_error("Invalid leaf count");
        }
        if (stats.entries != tree.size()) {
            throw std::logic_error("Invalid entry count");
        }

        json result = json::object();
        result["lambda"] = tree.lambda();
        result["path"] = input;
        result["height"] = tree.height();
        result["mbb"] = to_string(stats.mbb);

        result["entry_count"] = tree.size();
        result["entry_area"] = div0(stats.entry_area, stats.entries);

        result["leaf_nodes"] = tree.leaf_node_count();
        result["leaf_utilization"] = div0(tree.size(), tree.leaf_node_count() * tree.max_leaf_entries());
        result["leaf_area"] = div0(stats.leaf_area, stats.leaves);

        result["internal_nodes"] = tree.internal_node_count();
        result["internal_area_ratio"] = div0(stats.internal_union_area_ratio, stats.internals);

        std::cout << result.dump(4) << std::endl;
        return 0;
    });
}

