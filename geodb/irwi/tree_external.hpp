#ifndef IRWI_TREE_EXTERNAL_HPP
#define IRWI_TREE_EXTERNAL_HPP

#include "geodb/irwi/base.hpp"
#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/inverted_index_external.hpp"
#include "geodb/utility/file_allocator.hpp"

#include <boost/noncopyable.hpp>
#include <tpie/blocks/block_collection_cache.h>
#include <tpie/serialization2.h>

namespace geodb {

template<size_t block_size>
struct tree_external;

template<size_t block_size, typename LeafData, u32 Lambda>
struct tree_external_impl;

/// Implementation of external storage for the irwi tree.
/// The main tree is implemented using a single file,
/// with blocks of size `block_size`.
///
/// Every internal node has its own inverted index.
/// The inverted index uses external memory with the same block size
/// as this class.
template<size_t block_size, typename LeafData, u32 Lambda>
struct tree_external_impl : boost::noncopyable {
    // Uses external storage for storing the inverted file for each node.
    using index_id_type = u64;
    using directory_allocator_type = directory_allocator<index_id_type>;
    using index_storage = inverted_index_external_storage<block_size>;

    using block_type = tpie::blocks::block;

    static constexpr size_t max_internal_entries() {
        size_t header = sizeof(index_id_type) + 4;  // Inverted index + child count.
        return (block_size - header) / sizeof(internal_entry);
    }

    static constexpr size_t max_leaf_entries() {
        size_t header = 4; // Child count.
        return (block_size - header) / sizeof(LeafData);
    }

    // block sizes are always the same.
    // the IRWI block_handle class wastes space by
    // additionaly storing the block size.
    struct block_handle {
        // for serialization support.
        static const bool is_trivially_serializable = true;

        u64 index = 0;

        block_handle() {}

        block_handle(tpie::blocks::block_handle h)
            : index(h.position / block_size)
        {
            geodb_assert(h.position % block_size == 0,
                         "position must be a multiple of block_size");
            geodb_assert(h.size == block_size, "block must have the correct size");
        }

        bool operator==(const block_handle& other) const {
            return index == other.index;
        }

        operator tpie::blocks::block_handle() const {
            return { index * block_size, block_size };
        }
    };

#pragma pack(push, 1)
    /// An entry of an internal node.
    struct internal_entry {
        bounding_box mmb;           ///< Minimum bounding box for the subtree of that child.
        block_handle ptr;      ///< Pointer to the child.
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


    // ----------------------------------------
    //      Storage interface.
    //  Used by the tree class and the tree cursor.
    // ----------------------------------------

    using index_type = inverted_index<index_storage, Lambda>;

    using index_handle = tpie::unique_ptr<index_type>;

    using const_index_handle = tpie::unique_ptr<const index_type>;

    /// Points to the location of a node of unknown type.
    struct node_ptr {
        node_ptr() {}
        node_ptr(block_handle handle): handle(handle) {}

        block_handle handle;

        bool operator==(const node_ptr& other) const {
            return handle == other.handle;
        }
    };

    /// Points to the location of an internal node on disk.
    struct internal_ptr {
        internal_ptr() {}
        internal_ptr(block_handle handle): handle(handle) {}

        block_handle handle;

        bool operator==(const internal_ptr& other) const {
            return handle == other.handle;
        }

        operator node_ptr() const { return node_ptr(handle); }
    };

    /// Points to the location of a leaf node on disk.
    struct leaf_ptr {
        leaf_ptr() {}
        leaf_ptr(block_handle handle): handle(handle) {}

        block_handle handle;

        bool operator==(const leaf_ptr& other) const {
            return handle == other.handle;
        }

        operator node_ptr() const { return node_ptr(handle); }
    };

    internal_ptr to_internal(node_ptr n) const {
        return internal_ptr(n.handle);
    }

    leaf_ptr to_leaf(node_ptr n) const {
        return leaf_ptr(n.handle);
    }

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

        return l;
    }

    index_handle index(internal_ptr i) {
        internal* n = get_internal(read_block(i));
        fs::path p = m_index_alloc.path(n->inverted_index);
        return tpie::make_unique<index_type>(
                    index_storage(std::move(p)));
    }

    const_index_handle const_index(internal_ptr i) const {
        return const_cast<tree_external_impl*>(this)->index(i);
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

    bounding_box get_mmb(internal_ptr i, u32 index) const {
        geodb_assert(index < max_internal_entries(), "index out of bounds");
        internal* n = get_internal(read_block(i));
        return n->entries[index].mmb;
    }

    void set_mmb(internal_ptr i, u32 index, const bounding_box& b) {
        geodb_assert(index < max_internal_entries(), "index out of bounds");
        internal* n = get_internal(read_block(i));
        n->entries[index].mmb = b;
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

    // ----------------------------------------
    //      Construction/Destruction
    // ----------------------------------------

    tree_external_impl(tree_external<block_size> params)
        : m_directory(std::move(params.directory))
        , m_index_alloc(ensure_directory(m_directory / "inverted_index"))
        , m_blocks((m_directory / "tree.blocks").string(), block_size, 32, true)
    {
        tpie::default_raw_file_accessor rf;
        if (rf.try_open_rw(state_path().string())) {
            rf.read_i(&m_size, sizeof(m_size));
            rf.read_i(&m_height, sizeof(m_height));
            rf.read_i(&m_root, sizeof(m_root));
        }
    }

    ~tree_external_impl() {
        tpie::default_raw_file_accessor rf;
        rf.open_rw_new(state_path().string());
        rf.write_i(&m_size, sizeof(m_size));
        rf.write_i(&m_height, sizeof(m_height));
        rf.write_i(&m_root, sizeof(m_root));
    }

    static fs::path ensure_directory(fs::path path) {
        fs::create_directories(path);
        return path;
    }

    fs::path state_path() const {
        return m_directory / "tree.state";
    }

    fs::path m_directory;
    size_t m_size = 0;              ///< Number of items in the tree.
    size_t m_height = 0;            ///< 0: Empty, 1: Only root (leaf), 2: everything else.
    block_handle m_root;            ///< Handle to root block (if any).
    mutable directory_allocator_type m_index_alloc;         ///< Allocator for inverted-index directories.
    mutable tpie::blocks::block_collection_cache m_blocks;  ///< Block cache for rtree nodes.
};

/// Instructs the irwi tree to use external storage with the specified
/// block size.
///
/// \tparam block_size
///     Block size used for external memory.
template<size_t block_size>
struct tree_external {
    template<typename LeafData, u32 Lambda>
    using implementation = tree_external_impl<block_size, LeafData, Lambda>;

    tree_external(fs::path directory):
        directory(directory) {}

    fs::path directory;
};

} // namespace geodb

#endif // IRWI_TREE_EXTERNAL_HPP
