#ifndef GEODB_IRWI_TREE_EXTERNAL_HPP
#define GEODB_IRWI_TREE_EXTERNAL_HPP

#include "geodb/hybrid_buffer.hpp"
#include "geodb/hybrid_map.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/block_handle.hpp"
#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/inverted_index_external.hpp"
#include "geodb/utility/as_const.hpp"
#include "geodb/utility/file_allocator.hpp"
#include "geodb/utility/movable_adapter.hpp"
#include "geodb/utility/raw_stream.hpp"
#include "geodb/utility/shared_values.hpp"

#include <boost/noncopyable.hpp>
#include <fmt/format.h>
#include <tpie/blocks/block_collection_cache.h>

namespace geodb {

template<size_t block_size, size_t fanout_leaf = 0, size_t fanout_internal = fanout_leaf>
struct tree_external;

template<size_t block_size, size_t fanout_leaf, size_t fanout_internal, typename LeafData, u32 Lambda>
struct tree_external_impl;

/// Implementation of external storage for the irwi tree.
/// The main tree is implemented using a single file,
/// with blocks of size `block_size`.
///
/// Every internal node has its own inverted index.
/// The inverted index uses external memory with the same block size
/// as this class.
template<size_t block_size, size_t fanout_leaf, size_t fanout_internal, typename LeafData, u32 Lambda>
class tree_external_impl : boost::noncopyable {
private:
    // Uses external storage for storing the inverted file for each node.
    using index_id_type = u64;

    using directory_allocator_type = directory_allocator<index_id_type>;

    using index_storage = inverted_index_external<block_size>;

    using block_type = tpie::blocks::block;

    using block_handle_type = block_handle<block_size>;

public:
    static constexpr size_t get_block_size() {
        return block_size;
    }

    static constexpr size_t max_internal_entries() {
        if (fanout_internal) {
            return fanout_internal;
        }

        size_t header = sizeof(index_id_type) + 4;  // Inverted index + child count.
        return (block_size - header) / sizeof(internal_entry);
    }

    static constexpr size_t max_leaf_entries() {
        if (fanout_leaf) {
            return fanout_leaf;
        }

        size_t header = 4; // Child count.
        return (block_size - header) / sizeof(LeafData);
    }

private:

#pragma pack(push, 1)
    /// An entry of an internal node.
    struct internal_entry {
        bounding_box mbb;           ///< Minimum bounding box for the subtree of that child.
        block_handle_type ptr;      ///< Pointer to the child.
    };

    /// Represents an internal node in main memory and on disk.
    struct internal {
        index_id_type inverted_index;                   ///< Location of the inverted index.
        u32 count;                                      ///< Number of internal entries.
        internal_entry entries[max_internal_entries()]; ///< Exactly count entries.
    };

    static_assert(sizeof(internal) <= block_size, "Node too large");

    /// Represents an internal node in main memory and on disk.
    /// Leaf entries do not have an inverted index, they are
    /// exactly the same as R-Tree leaves.
    struct leaf {
        u32 count;                              ///< Number of children.
        LeafData entries[max_leaf_entries()];   ///< Exactly count entries.
    };

    static_assert(sizeof(leaf) <= block_size, "Node too large");
#pragma pack(pop)

    internal* get_internal(block_type* block) const {
        return reinterpret_cast<internal*>(block->get());
    }

    leaf* get_leaf(block_type* block) const {
        return reinterpret_cast<leaf*>(block->get());
    }

    template<typename Pointer>
    block_type* read_block(Pointer p) const {
        return m_blocks.read_block(p.handle);
    }

    template<typename Pointer>
    void write_block(Pointer p) {
        m_blocks.write_block(p.handle);
    }

public:
    using index_type = inverted_index<index_storage, Lambda>;

private:
    using index_instances_type = shared_instances<index_id_type, index_type>;

public:
    // ----------------------------------------
    //      Storage interface.
    //  Used by the tree class and the tree cursor.
    // ----------------------------------------

    using node_id = u64;

    using index_ptr = typename index_instances_type::pointer;

    using const_index_ptr = typename index_instances_type::const_pointer;

    /// Points to the location of a node of unknown type.
    struct node_ptr {
        node_ptr() {}
        node_ptr(block_handle_type handle): handle(handle) {}

        block_handle_type handle;

        bool operator==(const node_ptr& other) const {
            return handle == other.handle;
        }
    };

    /// Points to the location of an internal node on disk.
    struct internal_ptr : node_ptr {
        using node_ptr::node_ptr;
    };

    /// Points to the location of a leaf node on disk.
    struct leaf_ptr : node_ptr {
        using node_ptr::node_ptr;
    };

    internal_ptr to_internal(node_ptr n) const {
        return internal_ptr(n.handle);
    }

    leaf_ptr to_leaf(node_ptr n) const {
        return leaf_ptr(n.handle);
    }

    node_id get_id(const node_ptr& n) const { return n.handle.index(); }

    size_t get_height() const { return m_height; }

    void set_height(size_t height) { m_height = height; }

    size_t get_size() const { return m_size; }

    void set_size(size_t size) { m_size = size; }

    node_ptr get_root() const { return node_ptr(m_root); }

    void set_root(node_ptr n) { m_root = n.handle; }

    internal_ptr create_internal() {
        internal_ptr i(m_blocks.get_free_block());

        internal* n = get_internal(read_block(i));
        n->inverted_index = m_index_alloc.alloc();
        n->count = 0;
        for (u32 i = 0; i < max_internal_entries(); ++i) {
            n->entries[i] = internal_entry();
        }
        write_block(i);

        ++m_internal_count;
        return i;
    }

    leaf_ptr create_leaf() {
        leaf_ptr l(m_blocks.get_free_block());

        leaf* n = get_leaf(read_block(l));
        n->count = 0;
        for (u32 i = 0; i < max_leaf_entries(); ++i) {
            n->entries[i] = LeafData();
        }
        write_block(l);

        ++m_leaf_count;
        return l;
    }

    index_ptr index(internal_ptr i) {
        internal* n = get_internal(read_block(i));
        return open_index(n->inverted_index);
    }

    const_index_ptr const_index(internal_ptr i) const {
        internal* n = get_internal(read_block(i));
        return open_index(n->inverted_index);
    }

    u32 get_count(internal_ptr i) const {
        internal* n = get_internal(read_block(i));
        return n->count;
    }

    void set_count(internal_ptr i, u32 count) {
        geodb_assert(count <= max_internal_entries(), "invalid count");
        internal* n = get_internal(read_block(i));
        n->count = count;
        write_block(i);
    }

    bounding_box get_mbb(internal_ptr i, u32 index) const {
        geodb_assert(index < max_internal_entries(), "index out of bounds");
        internal* n = get_internal(read_block(i));
        return n->entries[index].mbb;
    }

    void set_mbb(internal_ptr i, u32 index, const bounding_box& b) {
        geodb_assert(index < max_internal_entries(), "index out of bounds");
        internal* n = get_internal(read_block(i));
        n->entries[index].mbb = b;
        write_block(i);
    }

    node_ptr get_child(internal_ptr i, u32 index) const {
        geodb_assert(index < max_internal_entries(), "index out of bounds");
        internal* n = get_internal(read_block(i));
        return node_ptr(n->entries[index].ptr);
    }

    void set_child(internal_ptr i, u32 index, node_ptr c) {
        geodb_assert(index < max_internal_entries(), "index out of bounds");
        internal* n = get_internal(read_block(i));
        n->entries[index].ptr = c.handle;
        write_block(i);
    }

    u32 get_count(leaf_ptr l) const {
        leaf* n = get_leaf(read_block(l));
        return n->count;
    }

    void set_count(leaf_ptr l, u32 count) {
        geodb_assert(count <= max_leaf_entries(), "invalid count");
        leaf* n = get_leaf(read_block(l));
        n->count = count;
        write_block(l);
    }

    LeafData get_data(leaf_ptr l, u32 index) const {
        geodb_assert(index < max_leaf_entries(), "index out of bounds");
        leaf* n = get_leaf(read_block(l));
        return n->entries[index];
    }

    void set_data(leaf_ptr l, u32 index, const LeafData& data) {
        geodb_assert(index < max_leaf_entries(), "index out of bounds");
        leaf* n = get_leaf(read_block(l));
        n->entries[index] = data;
        write_block(l);
    }

    size_t get_leaf_count() const {
        return m_leaf_count;
    }

    size_t get_internal_count() const {
        return m_internal_count;
    }

    template<typename Key, typename Value>
    using map_type = hybrid_map<Key, Value, block_size>;

    template<typename Key, typename Value>
    map_type<Key, Value> make_map() {
        // TODO: Could assign a special directory instead of global /tmp/
        return map_type<Key, Value>();
    }

    template<typename Value>
    using buffer_type = hybrid_buffer<Value, block_size>;

    template<typename Value>
    buffer_type<Value> make_buffer() {
        return buffer_type<Value>();
    }

public:
    static constexpr int version() { return 2; }

    // ----------------------------------------
    //      Construction/Destruction
    // ----------------------------------------
    tree_external_impl(const fs::path& directory)
        : m_directory(directory)
        , m_index_alloc(ensure_directory(directory / "inverted_index"))
        , m_blocks((directory / "tree.blocks").string(), block_size, 32, true)
    {
        raw_stream rf;
        if (rf.try_open(state_path())) {
            int file_version;
            rf.read(file_version);
            if (file_version != version()) {
                throw std::invalid_argument(fmt::format("Invalid file format version. Expected {} but got {}.",
                                                        version(), file_version));
            }

            size_t file_block_size;
            rf.read(file_block_size);
            if (file_block_size != block_size) {
                throw std::invalid_argument(fmt::format("Invalid block size. Expected {} but got {}.",
                                                        block_size, file_block_size));
            }


            size_t file_lambda;
            rf.read(file_lambda);
            if (file_lambda != Lambda) {
                throw std::invalid_argument(fmt::format("Invalid lambda value. Expected {} but got {}.",
                                                        Lambda, file_lambda));
            }

            size_t file_internal_fanout;
            rf.read(file_internal_fanout);
            if (file_internal_fanout != max_internal_entries()) {
                throw std::invalid_argument(fmt::format("Invalid internal node fanout. Expected {} but got {}.",
                                                        max_internal_entries(), file_internal_fanout));
            }

            size_t file_leaf_fanout;
            rf.read(file_leaf_fanout);
            if (file_leaf_fanout != max_leaf_entries()) {
                throw std::invalid_argument(fmt::format("Invalid leaf node fanout. Expected {} but got {}.",
                                                        max_leaf_entries(), file_leaf_fanout));
            }

            rf.read(m_size);
            rf.read(m_height);
            rf.read(m_leaf_count);
            rf.read(m_internal_count);
            rf.read(m_root);
        }
    }

    ~tree_external_impl() {
        raw_stream rf;
        rf.open_new(state_path());

        int file_version = version();
        rf.write(file_version);

        size_t file_block_size = block_size;
        size_t file_lambda = Lambda;
        size_t file_internal_fanout = max_internal_entries();
        size_t file_leaf_fanout = max_leaf_entries();
        rf.write(file_block_size);
        rf.write(file_lambda);
        rf.write(file_internal_fanout);
        rf.write(file_leaf_fanout);

        rf.write(m_size);
        rf.write(m_height);
        rf.write(m_leaf_count);
        rf.write(m_internal_count);
        rf.write(m_root);
    }

private:
    fs::path state_path() const {
        return m_directory / "tree.state";
    }

    const_index_ptr open_index(index_id_type id) const {
        return m_indexes.open(id, [&]{
            fs::path p = m_index_alloc.path(id);
            return index_type(index_storage(std::move(p)));
        });
    }

    index_ptr open_index(index_id_type id) {
        return m_indexes.convert(as_const(this)->open_index(id));
    }

private:
    /// Directory path of this tree.
    fs::path m_directory;

    /// Number of items in the tree.
    size_t m_size = 0;

    /// 0: Empty, 1: Only root (leaf), 2: everything else.
    size_t m_height = 0;

    /// Number of leaf nodes.
    size_t m_leaf_count = 0;

    /// Number of internal nodes.
    size_t m_internal_count = 0;

    /// Always points to either a leaf (height: 1) or an internal node (anything else).
    block_handle_type m_root;

    /// Allocator for inverted-index directories.
    mutable directory_allocator_type m_index_alloc;

    /// Block cache for rtree nodes.
    mutable tpie::blocks::block_collection_cache m_blocks;

    /// Collection of opened index instances.
    mutable index_instances_type m_indexes;
};

/// Instructs the irwi tree to use external storage with the specified
/// block size.
///
/// \tparam block_size
///     Block size used for external memory.
/// \tparam fanout_leaf
///     The custom fanout for leaf nodes.
///     A value of zero (the default) will choose the maximum fanout for
///     the given block size.
/// \tparam fanout_internal
///     The custom fanout for internal nodes.
///     A value of zero will choose the maximum fanout for the given block size.
///     Defaults to `fanout_leaf`.
template<size_t block_size, size_t fanout_leaf, size_t fanout_internal>
class tree_external {
private:
    fs::path directory;

public:
    tree_external(fs::path directory):
        directory(directory) {}

private:
    template<typename StorageSpec, typename Value, typename Accessor, u32 Lambda>
    friend class tree_state;

    template<typename LeafData, u32 Lambda>
    using implementation = tree_external_impl<block_size, fanout_leaf, fanout_internal, LeafData, Lambda>;

    template<typename LeafData, u32 Lambda>
    movable_adapter<implementation<LeafData, Lambda>>
    construct() const {
        return { in_place_t(), directory };
    }
};

} // namespace geodb

#endif // GEODB_IRWI_TREE_EXTERNAL_HPP
