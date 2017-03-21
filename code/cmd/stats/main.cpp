#include "common/common.hpp"

#include <boost/program_options.hpp>
#include <fmt/ostream.h>

#include <iostream>
#include <string>

using namespace std;
using namespace geodb;
namespace po = boost::program_options;

static std::string input;

void parse_options(int argc, char** argv) {
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

struct tree_stats {
    double entry_area = 0.0;
    u64 entries = 0.0;

    double leaf_area = 0.0;
    u64 leaves = 0;
};

double normalized_size(const bounding_box& operand, const bounding_box& all){
    return double(operand.size()) / all.size();
}

void analyse(typename external_tree::cursor& node, tree_stats& stats) {
    if (node.is_leaf()) {
        for (size_t i = 0; i < node.size(); ++i) {
            stats.entry_area += node.mbb(i).size();
            stats.entries += 1;
        }

        stats.leaf_area += node.mbb().size();
        stats.leaves += 1;
    } else {
        for (size_t i = 0; i < node.size(); ++i) {
            node.move_child(i);
            analyse(node, stats);
            node.move_parent();
        }
    }
}

tree_stats analyse(const external_tree& tree) {
    tree_stats stats;

    if (!tree.empty()) {
        auto root = tree.root();
        const bounding_box all = root.mbb();

        analyse(root, stats);

        stats.entry_area /= all.size();
        stats.leaf_area /= all.size();
    }

    return stats;
}

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        external_tree tree{external_storage(input)};

        tree_stats stats = analyse(tree);
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
        result["size"] = tree.size();
        result["leaf_nodes"] = tree.leaf_node_count();
        result["leaf_utilization"] = double(tree.size()) / double(tree.leaf_node_count()) / tree.max_leaf_entries();
        result["internal_nodes"] = tree.internal_node_count();
        result["nodes"] = tree.node_count();
        result["data_density"] = stats.entry_area / stats.entries;
        result["leaf_density"] = stats.leaf_area / stats.leaves;

        std::cout << result.dump(4) << std::endl;
        return 0;
    });
}

