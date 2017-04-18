#ifndef GEODB_IRWI_TREE_QUICKLOAD_HPP
#define GEODB_IRWI_TREE_QUICKLOAD_HPP

#include "geodb/external_list.hpp"
#include "geodb/hybrid_buffer.hpp"
#include "geodb/hybrid_map.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/block_collection.hpp"
#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/inverted_index_external.hpp"
#include "geodb/utility/as_const.hpp"
#include "geodb/utility/file_allocator.hpp"
#include "geodb/utility/shared_values.hpp"
#include "geodb/utility/temp_dir.hpp"

#include <boost/container/static_vector.hpp>

/// \file
/// Storage implementation for the quickload algorithm.
/// Data is only temporary, many entries are kept in RAM.

namespace geodb {

namespace quickload {

template<size_t block_size, u32 fanout_leaf, u32 fanout_internal = fanout_leaf>
class tree_storage;

template<size_t block_size, u32 fanout_leaf, u32 fanout_internal, typename LeafData>
class tree_storage_impl;

template<size_t block_size, size_t max_posting_entries>
class index_storage;

template<size_t block_size, size_t max_posting_entries>
class index_storage_impl;

template<size_t block_size, size_t max_entries>
class postings_list_storage;

template<size_t block_size, size_t max_entries>
class postings_list_storage_impl;

#pragma pack(push, 1)

template<size_t max_entries>
struct postings_list_entry {
    u32 count = 0;
    posting<0> postings[max_entries];
};

#pragma pack(pop)

template<size_t block_size, size_t max_entries>
using postings_list_backend = external_list<postings_list_entry<max_entries>, block_size>;

template<size_t block_size, size_t max_entries>
class postings_list_storage_impl {
private:
    using posting_type = posting<0>;

public:
    using entry = postings_list_entry<max_entries>;

    using backend_type = postings_list_backend<block_size, max_entries>;

    using posting_vector = boost::container::static_vector<posting_type, max_entries>;

    using iterator = typename posting_vector::const_iterator;

public:
    /// Constructs a posting list storage backend that has its
    /// data at the given index.
    postings_list_storage_impl(backend_type& list, u64 index)
        : m_list(list)
        , m_index(index)
    {

        // state.postings is not aligned.
        entry state = m_list[index];
        for (u32 i = 0; i < state.count; ++i) {
            m_postings.push_back(state.postings[i]);
        }
    }

    postings_list_storage_impl(postings_list_storage_impl&& other) noexcept
        : m_list(other.m_list)
        , m_index(other.m_index)
        , m_dirty(other.m_dirty)
        , m_postings(other.m_postings)
    {
        other.m_dirty = false;
    }

    ~postings_list_storage_impl() {
        if (m_dirty) {
            // Write back the changes to the list.
            entry state;
            state.count = size();
            for (u32 i = 0; i < state.count; ++i) {
                memcpy(&state.postings[i], &m_postings[i], sizeof(posting_type));
            }

            m_list.set(m_index, state);
        }
    }

public:
    iterator begin() const { return m_postings.begin(); }

    iterator end() const { return m_postings.end(); }

    void push_back(const posting_type& value) {
        geodb_assert(size() < max_entries, "Too many entries in postings list");
        m_postings.push_back(value);
        m_dirty = true;
    }

    void pop_back() {
        geodb_assert(size() > 0, "Must not be empty.");
        m_postings.pop_back();
        m_dirty = true;
    }

    void clear() {
        if (!m_postings.empty()) {
            m_postings.clear();
            m_dirty = true;
        }
    }

    void set(iterator pos, const posting_type& value) {
        geodb_assert(pos != end(), "Writing to the end iterator");
        u32 index = pos - begin();
        m_postings[index] = value;
        m_dirty = true;
    }

    size_t size() const {
        return m_postings.size();
    }

private:
    /// The shared list stores multiple postings lists per block.
    backend_type& m_list;

    /// The index in the shared list.
    u64 m_index;

    /// True if any value changed over the existance of this instance.
    bool m_dirty = false;

    /// Cached entry data. The inverted index guarantees that this list instance is the
    /// only one modifying it.
    posting_vector m_postings;
};

template<size_t block_size, size_t max_entries>
class postings_list_storage {
    postings_list_backend<block_size, max_entries>& list;
    u64 index;

public:
    postings_list_storage(postings_list_backend<block_size, max_entries>& list, u64 index)
        : list(list), index(index)
    {}

private:
    template<typename StorageSpec, u32 Lambda>
    friend class postings_list;

    template<typename Posting>
    using implementation = postings_list_storage_impl<block_size, max_entries>;

    template<typename Posting>
    implementation<Posting> construct() const {
        static_assert(std::is_same<Posting, posting<0>>::value, "Unsupported posting type.");
        return {list, index};
    }
};

template<size_t block_size, size_t max_posting_entries>
class index_storage_impl : boost::noncopyable {
public:
    using list_backend_type = postings_list_backend<block_size, max_posting_entries>;

    using list_storage_type = postings_list_storage<block_size, max_posting_entries>;

    using list_type = postings_list<list_storage_type, 0>;

    // Key: index into the shared list.
    using list_instances_type = shared_instances<u64, list_type>;

    using list_ptr = typename list_instances_type::pointer;

    using const_list_ptr = typename list_instances_type::const_pointer;

private:
    // TODO: Hybrid storage.
    /// Maps label to its list's index in the shared backend.
    using map_type = hybrid_map<label_type, u64, block_size>;

public:
    using iterator_type = typename map_type::const_iterator;

    iterator_type begin() const { return m_lists.begin(); }

    iterator_type end() const { return m_lists.end(); }

    iterator_type find(label_type label) const {
        return m_lists.find(label);
    }

    label_type label(iterator_type pos) const {
        geodb_assert(pos != end(), "dereferencing invalid iterator");
        return pos->first;
    }

    iterator_type create(label_type label) {
        geodb_assert(m_lists.find(label) == m_lists.end(), "already have label");
        u64 index = create_list();

        bool inserted = m_lists.insert(label, index);
        (void) inserted;
        geodb_assert(inserted, "already stored the label.");
        return m_lists.find(label);
    }

    list_ptr list(iterator_type pos) {
        geodb_assert(pos != end(), "dereferencing invalid iterator");
        return open_list(pos->second);
    }

    const_list_ptr const_list(iterator_type pos) const {
        geodb_assert(pos != end(), "dereferencing invalid iterator");
        return open_list(pos->second);
    }

    list_ptr total_list() {
        return open_list(m_total_index);
    }

    const_list_ptr const_total_list() const {
        return open_list(m_total_index);
    }

    size_t size() const {
        return m_lists.size();
    }

public:
    index_storage_impl(const fs::path& path, list_backend_type& postings_lists)
        : m_postings_lists(postings_lists)
        , m_total_index(create_list())
        , m_lists(path)
    {}

    ~index_storage_impl() {}

private:
    u64 create_list() {
        // Create a new list initialized with its zero state.
        u64 index = m_postings_lists.size();
        m_postings_lists.append(postings_list_entry<max_posting_entries>());
        return index;
    }

    const_list_ptr open_list(u64 index) const {
        return m_open_lists.open(index, [&]{
            return list_type(list_storage_type(m_postings_lists, index));
        });
    }

    list_ptr open_list(u64 index) {
        return m_open_lists.convert(as_const(this)->open_list(index));
    }

private:
    list_backend_type& m_postings_lists;
    u64 m_total_index;
    map_type m_lists;
    mutable list_instances_type m_open_lists;
};

template<size_t block_size, size_t max_posting_entries>
class index_storage {
    fs::path path;
    postings_list_backend<block_size, max_posting_entries>& backend;

public:
    index_storage(const fs::path& path,
                  postings_list_backend<block_size, max_posting_entries>& backend)
        : path(path), backend(backend) {}

private:
    template<typename StorageSpec, u32 Lambda>
    friend class inverted_index;

    template<u32 Lambda>
    using implementation = index_storage_impl<block_size, max_posting_entries>;

    template<u32 Lambda>
    movable_adapter<implementation<Lambda>>
    construct() const {
        static_assert(Lambda == 0, "Lambda must be 0.");
        return { in_place_t(), path, backend };
    }
};

/// Storage backend for the quickload algorithm. Mostly uses internal memory.
template<size_t block_size, u32 fanout_leaf, u32 fanout_internal, typename LeafData>
class tree_storage_impl : boost::noncopyable {
    /// Every list can have at most fanout_internal entries,
    /// because an internal node cannot have more children than that.
    static constexpr size_t max_posting_entries = fanout_internal;

    using index_storage_type = index_storage<block_size, max_posting_entries>;

    using dir_alloc_t = directory_allocator<u64>;

public:
    static constexpr u32 max_internal_entries() { return fanout_internal; }

    static constexpr u32 max_leaf_entries() { return fanout_leaf; }

    // Using lambda == 0.
    using index_type = inverted_index<index_storage_type, 0>;

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
        internal(const fs::path& path,
                 postings_list_backend<block_size, fanout_internal>& lists_backend)
            : index(index_storage_type(path, lists_backend))
            , count(0)
        {}

        /// The index is stored inline and remains open.
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

        fs::path path = m_directory.path() / fmt::format("node-{}", m_internals);
        return tpie::tpie_new<internal>(path, m_lists_backend);
    }

    leaf_ptr create_leaf() {
        geodb_assert(!m_leaves_cut, "leaves have been cut off");
        ++m_leaves;
        return tpie::tpie_new<leaf>();
    }

private:
    void destroy_internal(internal_ptr i) {
        tpie::tpie_delete(i);
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
    tree_storage_impl(size_t max_leaves)
        : m_directory("quickload-tree")
        , m_lists_backend(ensure_directory(m_directory.path() / "posting_lists"),
                          list_cache_size(max_leaves))
    {
    }

    ~tree_storage_impl() {
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

    /// Shared external storage for all postings lists.
    postings_list_backend<block_size, fanout_internal> m_lists_backend;

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
class tree_storage {
private:
    size_t max_leaves;

public:
    /// \param max_leaves
    ///     The maximum number of leaves that the tree is expected to have.
    ///     From this value we can derive the maximum number of internal nodes
    ///     and compute an appropriate per-node cache size.
    tree_storage(size_t max_leaves)
        : max_leaves(max_leaves) {}

private:
    template<typename StorageSpec, typename Value, typename Accessor, u32 Lambda>
    friend class tree_state;

    template<typename LeafData, u32 Lambda>
    using implementation = tree_storage_impl<block_size, fanout_leaf, fanout_internal, LeafData>;

    template<typename LeafData, u32 Lambda>
    movable_adapter<implementation<LeafData, Lambda>>
    construct() const {
        static_assert(Lambda == 0, "quickload only uses Lambda == 0.");
        return {in_place_t(), max_leaves};
    }
};

} // namespace quickload
} // namespace geodb

#endif // GEODB_IRWI_TREE_QUICKLOAD_HPP
