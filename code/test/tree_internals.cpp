#include <catch.hpp>

#include "geodb/common.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/tree_insertion.hpp"
#include "geodb/irwi/tree_internal.hpp"
#include "geodb/irwi/tree_state.hpp"

using namespace geodb;

using storage_spec = tree_internal<16>;
using state_type = tree_state<storage_spec, tree_entry, geodb::detail::tree_entry_accessor, 10>;
using storage_type = typename state_type::storage_type;

using node_ptr = typename state_type::node_ptr;
using leaf_ptr = typename state_type::leaf_ptr;
using internal_ptr = typename state_type::internal_ptr;
using index_ptr = typename state_type::index_ptr;

using posting_type = typename state_type::posting_type;

using insertion_type = tree_insertion<state_type>;

struct test_tree {
public:
    test_tree()
        : m_state(storage_spec(), geodb::detail::tree_entry_accessor(), 0.5)
    {}

    state_type& state() { return m_state; }
    storage_type& storage() { return m_state.storage(); }

    void insert(const tree_entry& e) {
        insertion_type(state()).insert(e);
    }

    void insert_node(leaf_ptr p) {
        insertion_type(state()).insert_node(p);
    }

    void insert_node(internal_ptr p, size_t height, size_t size) {
        insertion_type(state()).insert_node(p, height, size);
    }

    void insert_entry(leaf_ptr p, const tree_entry& e) {
        insertion_type(state()).insert_entry(p, e);
    }

    template<typename NodePointer>
    void insert_entry(internal_ptr p, NodePointer child) {
        insertion_type(state()).insert_entry(p, child);
    }

private:
    state_type m_state;
};

TEST_CASE("insert into empty tree", "[tree-internals]") {
    test_tree tree;
    storage_type& storage = tree.storage();

    REQUIRE(storage.get_size() == 0);
    REQUIRE(storage.get_height() == 0);

    tree_entry entry{
        1, 2,
        trajectory_unit{point(1, 2, 3), point(4, 5, 6), 1}
    };

    tree.insert(entry);
    REQUIRE(storage.get_size() == 1);
    REQUIRE(storage.get_height() == 1);

    leaf_ptr leaf = storage.to_leaf(storage.get_root());
    REQUIRE(storage.get_count(leaf) == 1);
    REQUIRE(storage.get_data(leaf, 0) == entry);

    tree.insert(entry);
    REQUIRE(storage.get_root() == leaf);
    REQUIRE(storage.get_height() == 1);
    REQUIRE(storage.get_size() == 2);

    REQUIRE(storage.get_count(leaf) == 2);
    REQUIRE(storage.get_data(leaf, 1) == entry);
}

TEST_CASE("insert with leaf split", "[tree-internals]") {
    test_tree tree;
    storage_type& storage = tree.storage();

    for (int i = 0; i < int(state_type::max_leaf_entries()); ++i) {
        tree_entry entry(
            1, i, trajectory_unit(point(i, i, i), point(i + 1, i + 1, i +1), 1)
        );
        tree.insert(entry);
    }

    REQUIRE(storage.get_height() == 1);
    REQUIRE(storage.get_size() == state_type::max_leaf_entries());

    tree.insert(tree_entry{2, 0, trajectory_unit{point(), point(), 2}});
    REQUIRE(storage.get_height() == 2);
    REQUIRE(storage.get_size() == state_type::max_leaf_entries() + 1);

    internal_ptr root = storage.to_internal(storage.get_root());
    REQUIRE(storage.get_count(root) == 2);

    leaf_ptr child1, child2;
    child1 = storage.to_leaf(storage.get_child(root, 0));
    child2 = storage.to_leaf(storage.get_child(root, 1));

    REQUIRE(storage.get_count(child1) + storage.get_count(child2) == storage.get_size());
}


TEST_CASE("inserting a leaf node", "[tree-internals]") {
    auto test_tree_1 = []() {
        test_tree tree;
        storage_type& storage = tree.storage();

        leaf_ptr a = storage.create_leaf();
        leaf_ptr b = storage.create_leaf();

        internal_ptr p = storage.create_internal();
        storage.set_count(p, 2);
        storage.set_child(p, 0, a);
        storage.set_child(p, 1, b);
        storage.set_mbb(p, 0, bounding_box(point(0, 0, 0), point(1, 1, 1)));
        storage.set_mbb(p, 1, bounding_box(point(2, 2, 2), point(3, 3, 3)));

        index_ptr i = storage.index(p);
        i->total()->append(posting_type(0, 10, {1, 2, 3}));
        i->total()->append(posting_type(1, 10, {1, 2, 3}));

        i->find_or_create(1)->postings_list()->append(posting_type(0, 10, {1, 2, 3}));
        i->find_or_create(1)->postings_list()->append(posting_type(1, 10, {1, 2, 3}));

        storage.set_root(p);
        storage.set_height(2);
        storage.set_size(20);

        return tree;
    };

    test_tree tree = test_tree_1();
    storage_type& storage = tree.storage();

    leaf_ptr leaf = storage.create_leaf();
    storage.set_count(leaf, 2);
    storage.set_data(leaf, 0, tree_entry(
        1, 1, trajectory_unit(point(10, 10, 10), point(11, 11, 11), 10)
    ));
    storage.set_data(leaf, 1, tree_entry(
        1, 1, trajectory_unit(point(15, 15, 15), point(16, 16, 16), 10)
    ));

    tree.insert_node(leaf);

    REQUIRE(storage.get_size() == 22);
    REQUIRE(storage.get_height() == 2);

    internal_ptr root = storage.to_internal(storage.get_root());
    REQUIRE(storage.get_count(root) == 3);
    REQUIRE(storage.get_child(root, 2) == leaf);
    REQUIRE(storage.get_mbb(root, 2) == bounding_box(point(10, 10, 10), point(16, 16, 16)));

    index_ptr index = storage.index(root);

    auto iter = index->find(10);
    REQUIRE(iter != index->end());

    auto list = iter->postings_list();
    REQUIRE(list->size() == 1);
    REQUIRE(list->get(0).count() == 2);
}

TEST_CASE("inserting a subtree with larger height", "[tree-internals]") {
    test_tree tree;
    storage_type& storage = tree.storage();

    tree.insert(tree_entry(1, 0, trajectory_unit(point(0, 0, 0), point(5, 5, 5), 5)));
    const leaf_ptr old_root = storage.to_leaf(storage.get_root());

    const internal_ptr parent = storage.create_internal();
    {
        leaf_ptr leaf1 = storage.create_leaf();
        tree.insert_entry(leaf1, tree_entry(2, 1, trajectory_unit(point(10, 10, 10), point(15, 15, 15), 10)));
        tree.insert_entry(parent, leaf1);

        leaf_ptr leaf2 = storage.create_leaf();
        tree.insert_entry(leaf2, tree_entry(2, 2, trajectory_unit(point(16, 16, 16), point(20, 20, 20), 11)));
        tree.insert_entry(leaf2, tree_entry(2, 2, trajectory_unit(point(20, 20, 20), point(17, 17, 17), 11)));
        tree.insert_entry(parent, leaf2);
    }
    tree.insert_node(parent, 2, 3);

    REQUIRE(storage.get_root() == parent);
    REQUIRE(storage.get_height() == 2);
    REQUIRE(storage.get_size() == 4);

    REQUIRE(storage.get_count(parent) == 3);
    REQUIRE(storage.get_child(parent, 2) == old_root);
}

TEST_CASE("inserting a small subtree into a large tree", "[tree-internals]") {
    test_tree tree;
    storage_type& storage = tree.storage();

    // A tree of height 3 where every internal node has two children.
    // We're going to insert leaves (of height 1), which makes
    // evaluating the cost function necessary.
    internal_ptr p1 = storage.create_internal();

        internal_ptr p2 = storage.create_internal();

            leaf_ptr l1 = storage.create_leaf();
            tree.insert_entry(l1, tree_entry(1, 1, trajectory_unit(point(0, 0, 0), point(1, 1, 1), 10)));
            tree.insert_entry(p2, l1);

            leaf_ptr l2 = storage.create_leaf();
            tree.insert_entry(l2, tree_entry(1, 2, trajectory_unit(point(1, 1, 1), point(1, 1, 2), 10)));
            tree.insert_entry(p2, l2);

        tree.insert_entry(p1, p2);

        internal_ptr p3 = storage.create_internal();

            leaf_ptr l3 = storage.create_leaf();
            tree.insert_entry(l3, tree_entry(10, 1, trajectory_unit(point(100, 100, 100), point(105, 105, 105), 10)));
            tree.insert_entry(p3, l3);

            leaf_ptr l4 = storage.create_leaf();
            tree.insert_entry(l4, tree_entry(10, 2, trajectory_unit(point(105, 105, 106), point(100, 110, 120), 10)));
            tree.insert_entry(p3, l4);

        tree.insert_entry(p1, p3);

    storage.set_root(p1);
    storage.set_height(3);
    storage.set_size(4);

    // n1 should be inserted into p3 (the mbb needs the lowest enlargement).
    leaf_ptr n1 = storage.create_leaf();
    tree.insert_entry(n1, tree_entry(50, 1, trajectory_unit(point(95, 103, 100), point(104, 109, 125), 10)));
    tree.insert_node(n1);

    REQUIRE(storage.get_height() == 3);
    REQUIRE(storage.get_size() == 5);
    REQUIRE(storage.get_count(p3) == 3);
    REQUIRE(storage.get_child(p3, 2) == n1);
    REQUIRE(storage.get_mbb(p3, 2) == bounding_box(point(95, 103, 100), point(104, 109, 125)));

    // n2 fits into p2.
    leaf_ptr n2 = storage.create_leaf();
    tree.insert_entry(n2, tree_entry(51, 1, trajectory_unit(point(5, 4, 3), point(3, 4, 4), 10)));
    tree.insert_entry(n2, tree_entry(51, 2, trajectory_unit(point(3, 4, 5), point(0, 0, 10), 10)));
    tree.insert_node(n2);

    REQUIRE(storage.get_height() == 3);
    REQUIRE(storage.get_size() == 7);
    REQUIRE(storage.get_count(p2) == 3);
    REQUIRE(storage.get_child(p2, 2) == n2);
    REQUIRE(storage.get_mbb(p2, 2) == bounding_box(point(0, 0, 3), point(5, 4, 10)));

}
