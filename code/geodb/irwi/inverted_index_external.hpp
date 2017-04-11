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

template<size_t block_size, u32 Lambda>
class inverted_index_external_builder;

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

    template<u32 Lambda>
    using builder = inverted_index_external_builder<block_size, Lambda>;

    template<u32 Lambda>
    movable_adapter<builder<Lambda>>
    construct_builder() const {
        return { in_place_t(), directory, list_blocks };
    }
};

template<size_t block_size, u32 Lambda>
class inverted_index_external_common {
protected:
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

    using builder_type = tpie::btree_builder<
        value_type, tpie::btree_external,
        tpie::btree_blocksize<block_size>,
        tpie::btree_key<key_extract>>;

    using list_storage_type = postings_list_blocks<block_size>;

public:
    using list_type = postings_list<list_storage_type, Lambda>;

protected:
    inverted_index_external_common(const fs::path& directory,
                                   block_collection<block_size>& list_blocks)
        : m_directory(directory)
        , m_list_blocks(list_blocks)
    {}

    /// Create a new list in the block storage and return both its
    /// block number and the new instance.
    std::tuple<list_handle, list_type> create_list() {
        list_handle handle = m_list_blocks.get_free_block();
        // Create a new list instance to initialize the base page.
        list_type list(list_storage_type(m_list_blocks, handle, true));
        return std::make_tuple(handle, std::move(list));
    }

    /// Opens the postings list identified by the given handle.
    list_type open_list(list_handle handle) const {
        // False: reopen previous state.
        return list_type(list_storage_type(m_list_blocks, handle, false));
    }

    /// Path to the state file on disk.
    fs::path state_path() const {
        return m_directory / "index.state";
    }

    /// Path to the btree file.
    fs::path tree_path() const {
        return m_directory / "index.btree";
    }

    /// Tries to restore the state from the file on disk.
    /// Returns true if a previous state existed.
    bool read_state() {
        raw_stream rf;
        if (rf.try_open(state_path())) {
            rf.read(this->m_total);
            return true;
        }
        return false;
    }

    /// Writes this instance's state to disk.
    void write_state() {
        raw_stream rf;
        rf.open_new(state_path());
        rf.write(this->m_total);
    }

protected:
    /// Path to the inverted index directory on disk.
    fs::path m_directory;

    /// Block collection for postings list, shared between all nodes.
    /// The btree of this index maps labels to their lists' base blocks.
    block_collection<block_size>& m_list_blocks;

    /// Pointer to the postings list that contains index information
    /// about all units (i.e. independent of their label).
    list_handle m_total = 0;
};

/// External storage for an inverted index.
/// The lookup table for label -> postings file is stored inside a btree.
/// A file allocator is used to allocate a file for each individual label.
template<size_t block_size, u32 Lambda>
class inverted_index_external_impl
        : public inverted_index_external_common<block_size, Lambda>
        , boost::noncopyable
{
    using common_t = typename inverted_index_external_impl::inverted_index_external_common;

    using typename common_t::list_storage_type;
    using typename common_t::list_handle;

public:
    using typename common_t::list_type;

private:
    using typename common_t::map_type;
    using typename common_t::value_type;
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
        return open_list(this->m_total);
    }

    const_list_ptr const_total_list() const {
        return open_list(this->m_total);
    }

    size_t size() const {
        return m_btree.size();
    }

public:
    inverted_index_external_impl(const fs::path& directory, block_collection<block_size>& list_blocks)
        : common_t(directory, list_blocks)
        , m_btree(common_t::tree_path().string())
    {
        // Restore the pointer to the `total` postings list or
        // create it if it didn't exist.
        if (!this->read_state()) {
            this->m_total = create_list();
        }
    }

    ~inverted_index_external_impl() {
        this->write_state();
    }

private:
    list_handle create_list() {
        list_handle handle;
        std::tie(handle, std::ignore) = common_t::create_list();
        return handle;
    }

    const_list_ptr open_list(list_handle handle) const {
        return m_lists.open(handle, [&]{
            return common_t::open_list(handle);
        });
    }

    list_ptr open_list(list_handle handle) {
        return m_lists.convert(as_const(this)->open_list(handle));
    }

private:
    /// The btree maps labels to their postings list.
    map_type m_btree;

    /// Collection of opened posting lists.
    mutable list_instances_type m_lists;
};

/// Bulk loading support for inverted indices.
/// An index is a mapping from label index to posting list.
/// This class allows label indices to be pushed in sorted (and unique)
/// order and their posting lists to be filled at will.
template<size_t block_size, u32 Lambda>
class inverted_index_external_builder
        : public inverted_index_external_common<block_size, Lambda>
        , boost::noncopyable
{
    using common_t = typename inverted_index_external_builder::inverted_index_external_common;

    using typename common_t::list_handle;
    using typename common_t::builder_type;
    using typename common_t::value_type;

public:
    using typename common_t::list_type;

public:
    /// Constructs a builder for the given location on disk.
    /// A previous index must not exist at that location.
    inverted_index_external_builder(const fs::path& directory,
                                   block_collection<block_size>& list_blocks)
        : common_t(directory, list_blocks)
        , m_builder(common_t::tree_path().string())
    {
        if (this->read_state()) {
            throw std::logic_error("A previous state already exists, cannot build a new index!");
        }

        // Create the "total" list in the constructor because its required anyway.
        auto result = this->create_list();
        this->m_total = std::get<0>(result);
        this->m_total_list.emplace(std::move(std::get<1>(result)));
    }

    ~inverted_index_external_builder() {
        if (!m_built) {
            build();
        }
    }

    /// Returns a reference to the "total" posting list.
    list_type& total() {
        geodb_assert(m_total_list, "optional must never be empty.");
        return *m_total_list;
    }

    /// Inserts the label into the (proto-) tree and returns the associated posting list.
    /// The labels must be pushed in order and must not be repeated.
    /// The list instance must not outlive the builder.
    list_type push(label_type label) {
        if (m_built) {
            throw std::logic_error("build() has already been called");
        }

        auto result = common_t::create_list();

        value_type value;
        value.label_id = label;
        value.handle = std::get<0>(result);
        m_builder.push(value);

        return std::move(std::get<1>(result));
    }

    /// Finalize the building process. Cannot modify the tree with this builder anymore.
    void build() {
        if (m_built) {
            throw std::logic_error("build() called more than once.");
        }
        m_built = true;
        m_builder.build(); // do not care about the btree at this point.
        this->write_state();
    }

private:
    builder_type m_builder;

    // optional for delayed initializtion. never empty after construction.
    boost::optional<list_type> m_total_list;

    /// True if build() has been called once.
    bool m_built = false;
};

} // namespace geodb

#endif // GEODB_IRWI_INVERTED_INDEX_EXTERNAL_HPP
