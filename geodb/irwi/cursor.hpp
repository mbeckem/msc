#ifndef IRWI_CURSOR_HPP
#define IRWI_CURSOR_HPP

#include "geodb/common.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/irwi/base.hpp"

#include <vector>

namespace geodb {

/// A class used for navigation through the structure of a tree.
/// A cursor always points to a current node.
///
/// Instances of this class can be obtained by calling \ref tree::root()
/// on a non-empty IRWI-Tree.
///
/// Warning: The tree must not be modified while it is being inspected using a cursor.
template<typename Tree>
class tree_cursor {
public:
    using tree_type = Tree;

private:
    using storage_type = typename tree_type::storage_type;

    using node_ptr = typename tree_type::node_ptr;

    using internal_ptr = typename tree_type::internal_ptr;

    using leaf_ptr = typename tree_type::leaf_ptr;

public:
    using index_type = typename tree_type::index_type;

    using index_handle = typename tree_type::const_index_handle;

    using id_type = typename tree_type::node_id_type;

    using value_type = typename tree_type::value_type;

public:
    /// Use the `root()` function of a tree to obtain an instance.
    tree_cursor() = delete;

    // Access functions

    /// Returns the numeric id of the current node.
    id_type id() const {
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

    /// Returns the index of the this node within its parent.
    /// \pre `has_parent()`.
    size_t index() const {
        geodb_assert(has_parent(), "must have a parent");
        // this might be too slow because the caching layer is hit
        // for every call of get_child().
        const node_ptr c = current();
        const internal_ptr p = storage().to_internal(m_path[level() - 2]);
        return tree().index_of(p, c);
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

    /// Returns a cursor for the current internal node's inverted index.
    /// \pre `is_internal()`.
    /// \warning Every inverted index may only be opened once.
    index_handle inverted_index() const {
        geodb_assert(is_internal(), "must be an internal node");
        internal_ptr p = storage().to_internal(current());
        return storage().const_index(p);
    }

    /// Returns the bounding box for the given child index.
    /// \pre `index < size()`.
    bounding_box mmb(size_t index) const {
        geodb_assert(index < size(), "index out of bounds");
        if (is_leaf()) {
            leaf_ptr p = storage().to_leaf(current());
            return tree().get_mmb(storage().get_data(p, index));
        } else {
            return storage().get_mmb(storage().to_internal(current()), index);
        }
    }

    /// Returns the numeric id of the given child node.
    /// \pre `is_internal() && index < size()`.
    id_type child_id(size_t index) const {
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
    const tree_type& tree() const {
        geodb_assert(m_tree, "invalid tree pointer");
        return *m_tree;
    }

    const storage_type& storage() const {
        return tree().storage();
    }

    node_ptr current() const {
        geodb_assert(!m_path.empty(), "path must never be empty");
        return m_path.back();
    }

private:
    template<typename StorageSpec, u32 Lambda>
    friend class tree;

    explicit tree_cursor(const tree_type* tree): m_tree(tree) {}

    void add_to_path(node_ptr ptr) {
        m_path.push_back(ptr);
    }

private:
    const tree_type* m_tree = nullptr;
    std::vector<node_ptr> m_path;
};

} // namespace geodb

#endif // IRWI_CURSOR_HPP
