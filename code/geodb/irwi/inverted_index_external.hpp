#ifndef GEODB_IRWI_INVERTED_INDEX_EXTERNAL_HPP
#define GEODB_IRWI_INVERTED_INDEX_EXTERNAL_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/postings_list.hpp"
#include "geodb/irwi/postings_list_external.hpp"
#include "geodb/utility/as_const.hpp"
#include "geodb/utility/raw_stream.hpp"
#include "geodb/utility/shared_values.hpp"

#include <tpie/btree.h>
#include <tpie/memory.h>

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

public:
    /// \param directory
    ///     Path to the directory on disk.
    inverted_index_external(fs::path directory)
        : directory(std::move(directory))
    {}

private:
    template<typename StorageSpec, u32 Lambda>
    friend class inverted_index;

    template<u32 Lambda>
    using implementation = inverted_index_external_impl<block_size, Lambda>;

    template<u32 Lambda>
    movable_adapter<implementation<Lambda>>
    construct() const {
        return { in_place_t(), directory };
    }
};

/// External storage for an inverted index.
/// The lookup table for label -> postings file is stored inside a btree.
/// A file allocator is used to allocate a file for each individual label.
template<size_t block_size, u32 Lambda>
class inverted_index_external_impl : boost::noncopyable {

    using list_id_type = u64;

    using file_allocator_type = file_allocator<list_id_type>;

    struct value_type {
        label_type      label_id;   // unique label identifier
        list_id_type    file;       // points to postings file on disk
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

    using list_storage_type = postings_list_external;

public:
    using list_type = postings_list<list_storage_type, Lambda>;

private:
    using list_instances_type = shared_instances<list_id_type, list_type>;

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
        list_id_type list = m_alloc.alloc();
        m_btree.insert(value_type{label, list});
        return m_btree.find(label);
    }

    list_ptr list(iterator_type iter) {
        geodb_assert(iter != m_btree.end(), "dereferencing invalid iterator");
        return open_list(iter->file);
    }

    const_list_ptr const_list(iterator_type iter) const {
        geodb_assert(iter != m_btree.end(), "dereferencing invalid iterator");
        return open_list(iter->file);
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
    inverted_index_external_impl(const fs::path& directory)
        : m_directory(directory)
        , m_btree((directory / "index.btree").string())
        , m_alloc(ensure_directory(directory / "postings_lists"))
    {
        // Restore the pointer to the `total` postings list or
        // create it if it didn't exist.
        raw_stream rf;
        if (rf.try_open(state_path())) {
            rf.read(m_total);
        } else {
            m_total = m_alloc.alloc();
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

    const_list_ptr open_list(list_id_type id) const {
        return m_lists.open(id, [&]{
            fs::path p = m_alloc.path(id);
            return list_type(list_storage_type(std::move(p)));
        });
    }

    list_ptr open_list(list_id_type id) {
        return m_lists.convert(as_const(this)->open_list(id));
    }

private:
    /// Path to the inverted index directory on disk.
    fs::path m_directory;

    /// The btree maps labels to their postings list.
    map_type m_btree;

    /// Used to allocate new postings lists.
    file_allocator_type m_alloc;

    /// Pointer to the postings list that contains index information
    /// about all units (i.e. independent of their label).
    list_id_type m_total = 0;

    /// Collection of opened posting lists.
    mutable list_instances_type m_lists;
};

} // namespace geodb

#endif // GEODB_IRWI_INVERTED_INDEX_EXTERNAL_HPP
