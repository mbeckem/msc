#ifndef GEODB_IRWI_TREE_INTERNAL_HPP
#define GEODB_IRWI_TREE_INTERNAL_HPP

#include "geodb/hybrid_buffer.hpp"
#include "geodb/hybrid_map.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/inverted_index_internal.hpp"

/// \file
/// Internal storage backend for IRWI Trees.
/// Everything is kept in RAM.

namespace geodb {

template<u32 fanout_leaf, u32 fanout_internal = fanout_leaf>
struct tree_internal;

template<u32 fanout_leaf, u32 fanout_internal, typename LeafData, u32 Lambda>
struct tree_internal_impl;

/// Storage backend for an IRWI-Tree that stores its data
/// in internal memory.
/// Every node (internal and external) has the specified fanout.
///
/// As a user: do not use this class directly.
template<u32 fanout_leaf, u32 fanout_internal, typename LeafData, u32 Lambda>
class tree_internal_impl {

    using index_storage = inverted_index_internal_storage;

public:
    static constexpr u32 max_internal_entries() { return fanout_internal; }

    static constexpr u32 max_leaf_entries() { return fanout_leaf; }

    using index_type = inverted_index<index_storage, Lambda>;

private:

    // Base class for nodes.
    struct base {
#ifndef NDEBUG
        // Runtime type information for debug builds,
        // no overhead otherwise.
        virtual ~base() = default;
#endif
    };

    struct internal_entry {
        bounding_box mbb;
        base* ptr;
    };

    struct internal : base {
        /// The index is stored inline.
        index_type index;

        /// Total number of entries.
        u32 count;

        /// Pointers to child nodes and their MBBs.
        std::array<internal_entry, fanout_internal> entries;
    };

    struct leaf : base {
        /// Total number of entries.
        u32 count;

        /// Leaf data entries.
        std::array<LeafData, fanout_leaf> entries;
    };

    /// Cast a child node to its appropriate type.
    /// This is a checked cast in debug builds.
    template<typename Child>
    Child* cast(base* b) const {
        geodb_assert(b, "null pointer treated as valid node");
        return static_cast<Child*>(b);
    }

public:
    // ----------------------------------------
    //      Storage interface.
    //  Used by the tree class and the tree cursor.
    // ----------------------------------------

    using index_ptr = index_type*;

    using const_index_ptr = const index_type*;

    using node_ptr = base*;

    using internal_ptr = internal*;

    using leaf_ptr = leaf*;

    using node_id = uintptr_t;

    internal_ptr to_internal(node_ptr n) const { return cast<internal>(n); }

    leaf_ptr to_leaf(node_ptr n) const { return cast<leaf>(n); }

    node_id get_id(node_ptr p) { return reinterpret_cast<node_id>(p); }

    size_t get_height() const { return m_height; }

    void set_height(size_t height) { m_height = height; }

    size_t get_size() const { return m_size; }

    void set_size(size_t size) { m_size = size; }

    node_ptr get_root() const { return m_root; }

    void set_root(node_ptr n) { m_root = n; }

    internal_ptr create_internal() {
        ++m_internals;
        return tpie::tpie_new<internal>();
    }

    leaf_ptr create_leaf() {
        ++m_leaves;
        return tpie::tpie_new<leaf>();
    }

    index_ptr index(internal_ptr i) {
        return &i->index;
    }

    const_index_ptr const_index(internal_ptr i) const {
        return &i->index;
    }

    u32 get_count(internal_ptr i) const { return i->count; }

    void set_count(internal_ptr i, u32 count) { i->count = count; }

    bounding_box get_mbb(internal_ptr i, u32 index) const {
        return i->entries[index].mbb;
    }

    void set_mbb(internal_ptr i, u32 index, const bounding_box& b) {
        i->entries[index].mbb = b;
    }

    node_ptr get_child(internal_ptr i, u32 index) const {
        return i->entries[index].ptr;
    }

    void set_child(internal_ptr i, u32 index, node_ptr c) {
        i->entries[index].ptr = c;
    }

    u32 get_count(leaf_ptr l) const { return l->count; }

    void set_count(leaf_ptr l, u32 count) { l->count = count; }

    LeafData get_data(leaf_ptr l, u32 index) const {
        return l->entries[index];
    }

    void set_data(leaf_ptr l, u32 index, const LeafData& d) {
        l->entries[index] = d;
    }

    size_t get_internal_count() const {
        return m_internals;
    }

    size_t get_leaf_count() const {
        return m_leaves;
    }

    template<typename Key, typename Value>
    using map_type = internal_map<Key, Value>;

    template<typename Key, typename Value>
    map_type<Key, Value> make_map() {
        return map_type<Key, Value>();
    }

    template<typename Value>
    using buffer_type = internal_buffer<Value>;

    template<typename Value>
    buffer_type<Value> make_buffer() {
        return buffer_type<Value>();
    }

public:
    tree_internal_impl()
    {

    }

    tree_internal_impl(tree_internal_impl&& other) noexcept
        : m_height(other.m_height)
        , m_size(other.m_size)
        , m_root(other.m_root)
    {
        other.m_height = other.m_size = 0;
        other.m_root = nullptr;
    }

    ~tree_internal_impl() {
        if (m_root) {
            geodb_assert(m_height > 0, "height must be nonzero if a root exists");
            geodb_assert(m_size > 0, "size must be nonzero if a root exists");
            destroy(m_root, 1);
        }
    }

private:
    /// Delete the subtree rooted at ptr.
    /// Level is the level of ptr in the tree (the root is at level 1).
    void destroy(base* ptr, size_t level) {
        geodb_assert(level > 0, "");
        if (level == m_height) {
            tpie::tpie_delete(cast<leaf>(ptr));
            return;
        }

        internal* n = cast<internal>(ptr);
        for (size_t i = 0; i < n->count; ++i) {
            destroy(n->entries[i].ptr, level + 1);
        }
        tpie::tpie_delete(n);
    }

private:
    size_t m_height = 0;    ///< 0: Tree is empty. 1: Root is leaf, 2: Everything else
    size_t m_size = 0;      ///< Number of data items inside the tree.
    size_t m_leaves = 0;    ///< Number of leaf nodes.
    size_t m_internals = 0; ///< Number of internal nodes.
    base* m_root = nullptr;
};

/// Instructs the irwi tree to use internal memory for storage.
///
/// \tparam fanout_leaf
///     The maximum number of children in leaf nodes.
/// \tparam fanout_internal
///     The maximum number of children in internal nodes.
///     Defaults to fanout_leaf.
template<u32 fanout_leaf, u32 fanout_internal>
class tree_internal {
public:
    tree_internal() = default;

private:
    template<typename StorageSpec, typename Value, typename Accessor, u32 Lambda>
    friend class tree_state;

    template<typename LeafData, u32 Lambda>
    using implementation = tree_internal_impl<fanout_leaf, fanout_internal, LeafData, Lambda>;

    template<typename LeafData, u32 Lambda>
    implementation<LeafData, Lambda>
    construct() const {
        return {};
    }
};

} // namespace geodb

#endif // GEODB_IRWI_TREE_INTERNAL_HPP
