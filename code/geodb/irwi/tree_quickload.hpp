#ifndef GEODB_IRWI_TREE_QUICKLOAD_HPP
#define GEODB_IRWI_TREE_QUICKLOAD_HPP

#include "geodb/hybrid_buffer.hpp"
#include "geodb/hybrid_map.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/block_collection.hpp"
#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/inverted_index_external.hpp"
#include "geodb/utility/file_allocator.hpp"
#include "geodb/utility/temp_dir.hpp"

/// \file
/// Internal storage backend for IRWI Trees.
/// Everything is kept in RAM.

namespace geodb {

template<size_t block_size, u32 fanout_leaf, u32 fanout_internal = fanout_leaf>
struct tree_quickload;

template<size_t block_size, u32 fanout_leaf, u32 fanout_internal, typename LeafData, u32 Lambda>
struct tree_quickload_impl;

/// Storage backend for the quickload algorithm. Mostly uses internal memory.
template<size_t block_size, u32 fanout_leaf, u32 fanout_internal, typename LeafData, u32 Lambda>
class tree_quickload_impl : boost::noncopyable {

    using index_storage = inverted_index_external<block_size>;

    using dir_alloc_t = directory_allocator<u64>;

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
        internal(u32 directory_id,
                 const fs::path& directory_path,
                 block_collection<block_size>& list_blocks)
            : directory_id(directory_id)
            , index(index_storage(directory_path, list_blocks))
            , count(0)
        {}

        /// The directory allocated for this node (stores the btree index).
        u32 directory_id;

        /// The index is stored inline and remains open.
        /// TODO: hybrid storage!
        index_type index;

        /// Total number of entries.
        u32 count;

        /// Pointers to child nodes and their MBBs.
        std::array<internal_entry, fanout_internal> entries;
    };

    struct leaf : base {
        leaf(): count(0) {}

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
#ifdef GEODB_DEBUG
            // Do not check leaf pointers if the leaves have been cut off,
            // they are invalid anyway.
            if (!std::is_same<Child, leaf>::value || !m_leaves_cut) {
                auto child = dynamic_cast<Child*>(b);
                geodb_assert(child, "invalid cast");
            }
#endif
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
        u32 directory_id = m_alloc.alloc();
        internal_ptr i = tpie::tpie_new<internal>(directory_id, m_alloc.path(directory_id), m_list_blocks);
        return i;
    }

    leaf_ptr create_leaf() {
        geodb_assert(!m_leaves_cut, "leaves have been cut off");
        ++m_leaves;
        return tpie::tpie_new<leaf>();
    }

private:
    void destroy_internal(internal_ptr i) {
        u32 directory_id = i->directory_id;

        // First delete the node (closes open files),
        // then erase the directory.
        tpie::tpie_delete(i);
        m_alloc.free(directory_id);
    }

    void destroy_leaf(leaf_ptr l) {
        tpie::tpie_delete(l);
    }

public:

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

    u32 get_count(leaf_ptr l) const { return access(l)->count; }

    void set_count(leaf_ptr l, u32 count) { access(l)->count = count; }

    LeafData get_data(leaf_ptr l, u32 index) const {
        return access(l)->entries[index];
    }

    void set_data(leaf_ptr l, u32 index, const LeafData& d) {
        access(l)->entries[index] = d;
    }

    size_t get_internal_count() const {
        return m_internals;
    }

    size_t get_leaf_count() const {
        return m_leaves;
    }

    /// Free all leaves but keep references to them in their parents.
    /// Used by the quickload algorithm.
    /// The number of leaves (i.e. `get_leaf_count()`) remains unaffected.
    void cut_leaves() {
        geodb_assert(!m_leaves_cut, "leaves were already cut off");

        m_leaves_cut = true;
        if (m_root) {
            destroy_leaves(m_root, 1);
        }
    }

    bool leaves_cut() const {
        return m_leaves_cut;
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
    tree_quickload_impl(size_t max_leaves)
        : m_directory("quickload-tree")
        , m_alloc(ensure_directory(m_directory.path() / "inverted_index"))
        , m_list_blocks(m_directory.path() / "posting_lists.data", list_cache_size(max_leaves))
    {

    }

    ~tree_quickload_impl() {
        if (m_root) {
            geodb_assert(m_height > 0, "height must be nonzero if a root exists");
            geodb_assert(m_size > 0, "size must be nonzero if a root exists");
            if (m_leaves_cut) {
                destroy<true>(m_root, 1);
            } else {
                destroy<false>(m_root, 1);
            }
        }
    }

private:
    leaf* access(leaf* l) const {
        geodb_assert(!m_leaves_cut, "leaves have been cut off and cannot be accessed");
        return l;
    }

    /// Delete the subtree rooted at ptr.
    /// Level is the level of ptr in the tree (the root is at level 1).
    template<bool leaves_cut>
    void destroy(base* ptr, size_t level) {
        geodb_assert(level > 0, "");
        if (level == m_height) {
            if (!leaves_cut) {
                destroy_leaf(cast<leaf>(ptr));
            }
            return;
        }

        internal* n = cast<internal>(ptr);
        for (size_t i = 0; i < n->count; ++i) {
            destroy<leaves_cut>(n->entries[i].ptr, level + 1);
        }
        destroy_internal(n);
    }

    /// Destroy only the leaves and leave dangling pointers
    /// to them in internal nodes.
    void destroy_leaves(base* ptr, size_t level) {
        geodb_assert(level > 0, "");
        if (level == m_height) {
            destroy_leaf(cast<leaf>(ptr));
            return;
        }

        internal* n = cast<internal>(ptr);
        for (size_t i = 0; i < n->count; ++i) {
            destroy_leaves(n->entries[i].ptr, level + 1);
        }
    }

    static size_t list_cache_size(size_t max_leaves) {
        // FIXME
        return max_leaves;
    }

private:
    temp_dir m_directory;
    dir_alloc_t m_alloc;
    block_collection<block_size> m_list_blocks;

    size_t m_height = 0;    ///< 0: Tree is empty. 1: Root is leaf, 2: Everything else
    size_t m_size = 0;      ///< Number of data items inside the tree.
    size_t m_leaves = 0;    ///< Number of leaf nodes.
    size_t m_internals = 0; ///< Number of internal nodes.
    base* m_root = nullptr;
    bool m_leaves_cut = false;
};

/// Storage for the quickload algorithm.
///
/// \tparam block_size
///     The block size in external storage.
/// \tparam fanout_leaf
///     The maximum number of children in leaf nodes.
/// \tparam fanout_internal
///     The maximum number of children in internal nodes.
///     Defaults to fanout_leaf.
template<size_t block_size, u32 fanout_leaf, u32 fanout_internal>
class tree_quickload {
private:
    size_t max_leaves;

public:
    /// \param max_leaves
    ///     The maximum number of leaves that the tree is expected to have.
    ///     From this value we can derive the maximum number of internal nodes
    ///     and compute an appropriate per-node cache size.
    tree_quickload(size_t max_leaves)
        : max_leaves(max_leaves) {}

private:
    template<typename StorageSpec, typename Value, typename Accessor, u32 Lambda>
    friend class tree_state;

    template<typename LeafData, u32 Lambda>
    using implementation = tree_quickload_impl<block_size, fanout_leaf, fanout_internal, LeafData, Lambda>;

    template<typename LeafData, u32 Lambda>
    movable_adapter<implementation<LeafData, Lambda>>
    construct() const {
        return {in_place_t(), max_leaves};
    }
};

} // namespace geodb

#endif // GEODB_IRWI_TREE_QUICKLOAD_HPP
