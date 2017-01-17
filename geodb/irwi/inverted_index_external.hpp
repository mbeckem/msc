#ifndef IRWI_INVERTED_INDEX_EXTERNAL_HPP
#define IRWI_INVERTED_INDEX_EXTERNAL_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/postings_list.hpp"
#include "geodb/irwi/postings_list_external.hpp"

#include <tpie/btree.h>
#include <tpie/memory.h>

namespace geodb {

template<size_t block_size>
class inverted_index_external_storage;

template<size_t block_size, u32 Lambda>
class inverted_index_external_storage_impl;

/// A marker type that instructs the \ref inverted_index to use
/// external storage.
template<size_t block_size>
class inverted_index_external_storage {
public:
    template<u32 Lambda>
    using implementation = inverted_index_external_storage_impl<block_size, Lambda>;

    /// \param directory
    ///     Path to the directory on disk.
    inverted_index_external_storage(fs::path directory)
        : directory(std::move(directory))
    {}

    fs::path directory;
};

/// External storage for an inverted index.
/// The lookup table for label -> postings file is stored inside a btree.
/// A file allocator is used to allocate a file for each individual label.
template<size_t block_size, u32 Lambda>
class inverted_index_external_storage_impl : boost::noncopyable {
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

    using list_storage = postings_list_external;

private:
    // ----------------------------------------
    //      Storage interface for friends
    // ----------------------------------------

    template<typename S, u32 L>
    friend class inverted_index;

    using iterator_type = typename map_type::iterator;

    using list_type = postings_list<list_storage, Lambda>;

    using list_handle = tpie::unique_ptr<list_type>;

    using const_list_handle = tpie::unique_ptr<const list_type>;

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

    list_handle list(iterator_type iter) {
        geodb_assert(iter != m_btree.end(), "dereferencing invalid iterator");
        fs::path path = m_alloc.path(iter->file);
        return tpie::make_unique<list_type>(list_storage(std::move(path)));
    }

    const_list_handle const_list(iterator_type iter) const {
        return const_cast<inverted_index_external_storage_impl*>(this)->list(iter);
    }

    list_handle total_list() {
        return tpie::make_unique<list_type>(list_storage(m_alloc.path(m_total)));
    }

    const_list_handle const_total_list() const {
        return const_cast<inverted_index_external_storage_impl*>(this)->total_list();
    }

public:
    inverted_index_external_storage_impl(inverted_index_external_storage<block_size> params)
        : m_directory(std::move(params.directory))
        , m_btree((m_directory / "index.btree").string())
        , m_alloc(ensure_directory(m_directory / "postings_lists"))
    {
        tpie::default_raw_file_accessor rf;

        // Restore the pointer to the `total` postings list or
        // create it if it didn't exist.
        if (rf.try_open_rw(state_path().string())) {
            rf.read_i(&m_total, sizeof(m_total));
        } else {
            m_total = m_alloc.alloc();
        }
    }

    ~inverted_index_external_storage_impl() {
        tpie::default_raw_file_accessor rf;
        rf.open_rw_new(state_path().string());
        rf.write_i(&m_total, sizeof(m_total));
    }

private:
    static fs::path ensure_directory(fs::path p) {
        fs::create_directories(p);
        return p;
    }

    fs::path state_path() const {
        return m_directory / "index.state";
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
};

} // namespace geodb

#endif // IRWI_INVERTED_INDEX_EXTERNAL_HPP
