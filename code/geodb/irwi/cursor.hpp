#ifndef GEODB_IRWI_CURSOR_HPP
#define GEODB_IRWI_CURSOR_HPP

#include "geodb/common.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/type_traits.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/utility/range_utils.hpp"

#include <boost/range/iterator_range.hpp>

#include <vector>

/// \file
/// A cursor can be used to visit the nodes of a tree.

namespace geodb {

/// A class used for navigation through the structure of a tree.
/// A cursor always points to a valid node of its tree.
///
/// Instances of this class can be obtained by calling \ref tree::root().
///
/// Warning: The tree must not be modified while it is being inspected using a cursor.
template<typename State>
class tree_cursor {
private:
    using state_type = State;

    using storage_type = typename state_type::storage_type;

    using node_ptr = typename state_type::node_ptr;

    using internal_ptr = typename state_type::internal_ptr;

    using leaf_ptr = typename state_type::leaf_ptr;

public:
    using index_type = typename state_type::index_type;

    using index_ptr = typename state_type::const_index_ptr;

    using node_id = typename state_type::node_id;

    using value_type = typename state_type::value_type;

public:
    /// Use the `root()` function of a tree to obtain an instance.
    tree_cursor() = delete;

    // Access functions

    /// Returns the numeric id of the current node.
    node_id id() const {
        return storage().get_id(current());
    }

    /// Returns the level of the current node.
    /// The root is at level 1, thus the level is always in [1, tree_height].
    size_t level() const {
        return m_path.size();
    }

    /// True if the current node has a parent.
    bool has_parent() const {
        return level() > 1;
    }

    /// True if the current node is the root.
    bool is_root() const {
        return !has_parent();
    }

    /// Returns a range of node ids (the path of this node),
    /// from the root to the node itself.
    auto path() const {
        return m_path | transformed([&](node_ptr ptr) {
            return storage().get_id(ptr);
        });
    }

    /// Returns the index of the this node within its parent.
    /// \pre `has_parent()`.
    size_t index() const {
        geodb_assert(has_parent(), "must have a parent");
        // this might be too slow because the caching layer is hit
        // for every call of get_child().
        const node_ptr c = current();
        const internal_ptr p = storage().to_internal(m_path[level() - 2]);
        return state().index_of(p, c);
    }

    /// Returns true if the current node is a leaf.
    /// Otherwise, the node is an internal node.
    bool is_leaf() const {
        return level() == storage().get_height();
    }

    /// Returns true if the current node is an internal node.
    /// Otherwise, the node is a leaf.
    bool is_internal() const {
        return !is_leaf();
    }

    /// Returns the number of children or entries for the current node.
    size_t size() const {
        if (is_leaf()) {
            return storage().get_count(storage().to_leaf(current()));
        } else {
            return storage().get_count(storage().to_internal(current()));
        }
    }

    /// Returns the maximum number of children or entries for the current node.
    size_t max_size() const {
        if (is_leaf()) {
            return state_type::max_leaf_entries();
        } else {
            return state_type::max_internal_entries();
        }
    }

    /// Returns a cursor for the current internal node's inverted index.
    /// \pre `is_internal()`.
    /// \warning Every inverted index may only be opened once.
    index_ptr inverted_index() const {
        geodb_assert(is_internal(), "must be an internal node");
        internal_ptr p = storage().to_internal(current());
        return storage().const_index(p);
    }

    /// Returns the mbb of the current node (which includes all children).
    bounding_box mbb() const {
        if (is_leaf()) {
            return state().get_mbb(storage().to_leaf(current()));
        } else {
            return state().get_mbb(storage().to_internal(current()));
        }
    }

    /// Returns the bounding box for the given child index.
    /// \pre `index < size()`.
    bounding_box mbb(size_t index) const {
        geodb_assert(index < size(), "index out of bounds");
        if (is_leaf()) {
            leaf_ptr p = storage().to_leaf(current());
            return state().get_mbb(storage().get_data(p, index));
        } else {
            return storage().get_mbb(storage().to_internal(current()), index);
        }
    }

    /// Returns the numeric id of the given child node.
    /// \pre `is_internal() && index < size()`.
    node_id child_id(size_t index) const {
        geodb_assert(is_internal(), "not an internal node");
        geodb_assert(index < size(), "index out of bounds");
        internal_ptr n = storage().to_internal(current());
        return storage().get_id(storage().get_child(n, index));
    }

    /// Returns the value at the given index.
    /// \pre `is_leaf() && index < size()`.
    value_type value(size_t index) const {
        geodb_assert(is_leaf(), "not a leaf");
        geodb_assert(index < size(), "index out of bounds");
        leaf_ptr n = storage().to_leaf(current());
        return storage().get_data(n, index);
    }

    // Navigation

    /// Move to the root of the tree.
    void move_root() {
        geodb_assert(m_path.size() >= 1, "path must not be empty");
        m_path.resize(1); // drop anything but the root.
    }

    /// Move to the parent of this node.
    /// \pre `has_parent()`.
    void move_parent() {
        geodb_assert(has_parent(), "must have a parent");
        m_path.pop_back();
    }

    /// Move to the child with the given index.
    /// \pre `is_internal() && index < size()`.
    void move_child(size_t index) {
        geodb_assert(is_internal(),  "not an internal node");
        geodb_assert(index < size(), "index out of bounds");
        internal_ptr n = storage().to_internal(current());
        m_path.push_back(storage().get_child(n, index));
    }

    // Immutable navigation

    /// Returns a new cursor that points to the root of the tree.
    tree_cursor root() const {
        tree_cursor c = *this;
        c.move_root();
        return c;
    }

    /// Returns a new cursor that points to the parent of the current node.
    /// \pre 'has_parent()`.
    tree_cursor parent() const {
        tree_cursor c = *this;
        c.move_parent();
        return c;
    }

    /// Returns a new cursor that points to the child with the given index.
    /// \pre `is_internal() && index < size()`.
    tree_cursor child(size_t index) const {
        tree_cursor c = *this;
        c.move_child(index);
        return c;
    }

private:
    const state_type& state() const {
        geodb_assert(m_state, "invalid tree state");
        return *m_state;
    }

    const storage_type& storage() const {
        return state().storage();
    }

    node_ptr current() const {
        geodb_assert(!m_path.empty(), "path must never be empty");
        return m_path.back();
    }

    void add_to_path(node_ptr ptr) {
        m_path.push_back(ptr);
    }

public:
    explicit tree_cursor(const state_type* state, node_ptr root)
        : m_state(state)
    {
        add_to_path(root);
    }

private:
    const state_type* m_state = nullptr;
    std::vector<node_ptr> m_path;
};

} // namespace geodb

#endif // GEODB_IRWI_CURSOR_HPP
