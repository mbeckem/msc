#ifndef GEODB_IRWI_INVERTED_INDEX_EXTERNAL_HPP
#define GEODB_IRWI_INVERTED_INDEX_EXTERNAL_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/block_collection.hpp"
#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/postings_list.hpp"
#include "geodb/irwi/postings_list_blocks.hpp"
#include "geodb/utility/as_const.hpp"
#include "geodb/utility/raw_stream.hpp"
#include "geodb/utility/shared_values.hpp"

#include <tpie/btree.h>
#include <tpie/memory.h>

/// \file
/// External backend for the inverted index.

namespace geodb {

template<size_t block_size>
class inverted_index_external;

template<size_t block_size, u32 Lambda>
class inverted_index_external_impl;

/// A marker type that instructs the \ref inverted_index to use
/// external storage.
template<size_t block_size>
class inverted_index_external {
private:
    fs::path directory;
    block_collection<block_size>& list_blocks;

public:
    /// \param directory
    ///     Path to the directory on disk.
    inverted_index_external(fs::path directory, block_collection<block_size>& list_blocks)
        : directory(std::move(directory))
        , list_blocks(list_blocks)
    {}

private:
    template<typename StorageSpec, u32 Lambda>
    friend class inverted_index;

    template<u32 Lambda>
    using implementation = inverted_index_external_impl<block_size, Lambda>;

    template<u32 Lambda>
    movable_adapter<implementation<Lambda>>
    construct() const {
        return { in_place_t(), directory, list_blocks };
    }
};

/// External storage for an inverted index.
/// The lookup table for label -> postings file is stored inside a btree.
/// A file allocator is used to allocate a file for each individual label.
template<size_t block_size, u32 Lambda>
class inverted_index_external_impl : boost::noncopyable {
    using list_handle = block_handle<block_size>;

    struct value_type {
        label_type  label_id;   // unique label identifier
        list_handle handle;     // points to postings list in the shared storage.
    };

    struct key_extract {
        label_type operator()(const value_type& item) const {
            return item.label_id;
        }
    };

    using map_type = tpie::btree<
        value_type, tpie::btree_external,
        tpie::btree_blocksize<block_size>,
        tpie::btree_key<key_extract>
    >;

    using list_storage_type = postings_list_blocks<block_size>;

public:
    using list_type = postings_list<list_storage_type, Lambda>;

private:
    using list_instances_type = shared_instances<list_handle, list_type>;

public:
    // ----------------------------------------
    //      Storage interface for inverted index
    // ----------------------------------------

    using iterator_type = typename map_type::iterator;

    using list_ptr = typename list_instances_type::pointer;

    using const_list_ptr = typename list_instances_type::const_pointer;

    iterator_type begin() const { return m_btree.begin(); }

    iterator_type end() const { return m_btree.end(); }

    iterator_type find(label_type label) const {
        return m_btree.find(label);
    }

    label_type label(iterator_type iter) const {
        geodb_assert(iter != m_btree.end(), "dereferencing invalid iterator");
        return iter->label_id;
    }

    iterator_type create(label_type label) {
        // Insert does not return an iterator..
        list_handle handle = create_list();
        m_btree.insert(value_type{label, handle});
        return m_btree.find(label);
    }

    list_ptr list(iterator_type iter) {
        geodb_assert(iter != m_btree.end(), "dereferencing invalid iterator");
        return open_list(iter->handle);
    }

    const_list_ptr const_list(iterator_type iter) const {
        geodb_assert(iter != m_btree.end(), "dereferencing invalid iterator");
        return open_list(iter->handle);
    }

    list_ptr total_list() {
        return open_list(m_total);
    }

    const_list_ptr const_total_list() const {
        return open_list(m_total);
    }

    size_t size() const {
        return m_btree.size();
    }

public:
    inverted_index_external_impl(const fs::path& directory, block_collection<block_size>& list_blocks)
        : m_directory(directory)
        , m_list_blocks(list_blocks)
        , m_btree((directory / "index.btree").string())
    {
        // Restore the pointer to the `total` postings list or
        // create it if it didn't exist.
        raw_stream rf;
        if (rf.try_open(state_path())) {
            rf.read(m_total);
        } else {
            m_total = create_list();
        }
    }

    ~inverted_index_external_impl() {
        raw_stream rf;
        rf.open_new(state_path());
        rf.write(m_total);
    }

private:
    fs::path state_path() const {
        return m_directory / "index.state";
    }

    list_handle create_list() {
        list_handle handle = m_list_blocks.get_free_block();
        // Create a new list instance the initialize the base page.
        list_type(list_storage_type(m_list_blocks, handle, true));
        return handle;
    }

    const_list_ptr open_list(list_handle handle) const {
        return m_lists.open(handle, [&]{
            // False: restore previous instance.
            return list_type(list_storage_type(m_list_blocks, handle, false));
        });
    }

    list_ptr open_list(list_handle handle) {
        return m_lists.convert(as_const(this)->open_list(handle));
    }

private:
    /// Path to the inverted index directory on disk.
    fs::path m_directory;

    /// Block collection for postings list, shared between all nodes.
    /// The btree of this index maps labels to their lists' base blocks.
    block_collection<block_size>& m_list_blocks;

    /// The btree maps labels to their postings list.
    map_type m_btree;

    /// Pointer to the postings list that contains index information
    /// about all units (i.e. independent of their label).
    list_handle m_total = 0;

    /// Collection of opened posting lists.
    mutable list_instances_type m_lists;
};

} // namespace geodb

#endif // GEODB_IRWI_INVERTED_INDEX_EXTERNAL_HPP
