#ifndef GEODB_IRWI_POSTINGS_LIST_BLOCKS_HPP
#define GEODB_IRWI_POSTINGS_LIST_BLOCKS_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/block_collection.hpp"
#include "geodb/irwi/block_handle.hpp"
#include "geodb/utility/movable_adapter.hpp"

#include <boost/noncopyable.hpp>

/// \file
/// Postings list backend for shared block storage.

namespace geodb {

template<size_t block_size>
class postings_list_blocks;

template<typename Posting, size_t block_size>
class postings_list_blocks_impl;

/// Parameters for opening or creating a new posting list
/// in a shared block collection file.
template<size_t block_size>
class postings_list_blocks
{
private:
    block_collection<block_size>& m_blocks;
    block_handle<block_size> m_base;
    bool m_first_time;

public:
    /// \param blocks
    ///     The shared block collection.
    /// \param base
    ///     A handle to the base block of this list.
    ///     This can either be a previously unused block
    ///     or a block that has already served as a base block.
    /// \param first_time
    ///     True if a new instance should be created at the given base block.
    ///     False if some previous state exists which should be restored instead.
    postings_list_blocks(block_collection<block_size>& blocks,
                         block_handle<block_size> base,
                         bool first_time)
        : m_blocks(blocks)
        , m_base(base)
        , m_first_time(first_time)
    {}

private:
    template<typename StorageSpec, u32 Lambda>
    friend class postings_list;

    template<typename Posting>
    using implementation = postings_list_blocks_impl<Posting, block_size>;

    template<typename Posting>
    movable_adapter<implementation<Posting>>
    construct() const {
        return { in_place_t(), m_blocks, m_base, m_first_time };
    }
};

/// Implements a linked list of blocks in a shared block file.
/// Every list has a (constant) base block in which its state can be stored.
/// Data blocks are doubly linked.
template<typename Posting, size_t block_size>
class postings_list_blocks_impl : boost::noncopyable {
private:
    using handle_type = block_handle<block_size>;

    using block_type = tpie::blocks::block;

    using posting_type = Posting;

private:
    static constexpr handle_type invalid = handle_type(-1);

#pragma pack(push, 1)

    /// The content of the base block.
    struct state_type {
        size_t size;
        handle_type first;
        handle_type last;
    };

    struct data_header {
        /// Pointer to the previous block (if any).
        handle_type prev;

        /// Pointer to the next block (if any).
        handle_type next;

        /// Number of entries in this block.
        u32 count;
    };

    static constexpr u32 block_entry_count() {
        return (block_size - sizeof(data_header)) / sizeof(Posting);
    }

    static_assert(block_entry_count() >= 1, "block size too small to fit a single entry");

    /// The content of the data blocks.
    struct data_type {
        data_header hdr;

        /// Data array.
        Posting entries[block_entry_count()];
    };

#pragma pack(pop)

    static_assert(sizeof(state_type) <= block_size,
                  "State type too large");

    static_assert(sizeof(data_type) <= block_size,
                  "Data type too large");

private:
    block_type* read_block(handle_type h) const {
        geodb_assert(h != invalid, "accessing invalid block index");
        return m_blocks.read_block(h);
    }

    void write_block(handle_type h) {
        geodb_assert(h != invalid, "accessing invalid block index");
        m_blocks.write_block(h);
    }

    state_type* get_state(block_type* block) const {
        return reinterpret_cast<state_type*>(block->get());
    }

    data_type* get_data(block_type* block) const {
        return reinterpret_cast<data_type*>(block->get());
    }

    /// Loads the list state from the base block.
    void load_state() {
        state_type* s = get_state(read_block(m_base));
        m_size = s->size;
        m_first = s->first;
        m_last = s->last;
    }

    /// Save the list state into the base block.
    void save_state() {
        state_type* s = get_state(read_block(m_base));
        s->size = m_size;
        s->first = m_first;
        s->last = m_last;
        write_block(m_base);
    }

    /// Create a new data block.
    handle_type create_data() {
        handle_type h = m_blocks.get_free_block();

        data_type* d = get_data(read_block(h));
        d->hdr.prev = invalid;
        d->hdr.next = invalid;
        d->hdr.count = 0;
        memset(&d->entries, 0, sizeof(d->entries));
        write_block(h);

        return h;
    }

    u32 get_count(handle_type h) const {
        data_type* d = get_data(read_block(h));
        return d->hdr.count;
    }

    void set_count(handle_type h, u32 size) {
        geodb_assert(size <= block_entry_count(), "count too large");
        data_type* d = get_data(read_block(h));
        d->hdr.count = size;
        write_block(h);
    }

    handle_type get_next(handle_type h) const {
        data_type* d = get_data(read_block(h));
        return d->hdr.next;
    }

    void set_next(handle_type h, handle_type n) {
        data_type* d = get_data(read_block(h));
        d->hdr.next = n;
        write_block(h);
    }

    handle_type get_prev(handle_type h) const {
        data_type* d = get_data(read_block(h));
        return d->hdr.prev;
    }

    void set_prev(handle_type h, handle_type p) {
        data_type* d = get_data(read_block(h));
        d->hdr.prev = p;
        write_block(h);
    }

    posting_type get_entry(handle_type h, u32 index) const {
        geodb_assert(index < block_entry_count(), "index out of bounds");
        data_type* d = get_data(read_block(h));
        return d->entries[index];
    }

    void set_entry(handle_type h, u32 index, const posting_type& entry) {
        geodb_assert(index < block_entry_count(), "index out of bounds");
        data_type* d = get_data(read_block(h));
        d->entries[index] = entry;
        write_block(h);
    }

    void clear_entry(handle_type h, u32 index) {
        geodb_assert(index < block_entry_count(), "index out of bounds");
        data_type* d = get_data(read_block(h));
        memset(&d->entries[index], 0, sizeof(d->entries[index]));
    }

public:
    // Points to an element within a data node.
    // The end pointer is represented by (invalid, 0).
    class iterator : public boost::iterator_facade<
            iterator,                           // Derived
            posting_type,                       // Value
            boost::bidirectional_traversal_tag, // Traversal
            posting_type                        // "Reference"
        >
    {
        using base = typename iterator::iterator_facade;

    private:
        const postings_list_blocks_impl* list = nullptr;
        handle_type node = invalid; // Pointer to data node.
        u32 index = 0;              // Index in node.

    public:
        iterator() = default;

    private:
        friend class postings_list_blocks_impl;

        iterator(const postings_list_blocks_impl* list, handle_type node, u32 index)
            : list(list), node(node), index(index)
        {}

    private:
        friend class boost::iterator_core_access;

        posting_type dereference() const {
            geodb_assert(list, "dereferencing invalid iterator");
            geodb_assert(node != invalid, "dereferencing end iterator");
            geodb_assert(index < list->get_count(node), "index out of bounds");
            return list->get_entry(node, index);
        }

        void increment() {
            geodb_assert(list, "incrementing invalid iterator");
            if (node == invalid) {
                node = list->m_first;
                index = 0;
                return;
            }

            geodb_assert(index < list->get_count(node),
                         "index out of bounds");
            ++index;
            if (index == list->get_count(node)) {
                node = list->get_next(node);
                index = 0;
            }
        }

        void decrement() {
            geodb_assert(list, "decrementing invalid iterator");

            if (node == invalid) {
                node = list->m_last;
                if (node == invalid) {
                    index = 0;
                } else {
                    const u32 size = list->get_count(node);
                    geodb_assert(size > 0, "valid nodes must not be empty");
                    index = size - 1;
                }
                return;
            }

            if (index > 0) {
                --index;
                return;
            }

            node = list->get_prev(node);
            if (node == invalid) {
                index = 0;
                return;
            }

            const u32 count = list->get_count(node);
            geodb_assert(count > 0, "valid nodes must not be empty");
            index = count - 1;
        }

        bool equal(const iterator& other) const {
            geodb_assert(list == other.list, "comparing iterators of different lists");
            return node == other.node && index == other.index;
        }
    };

public:
    iterator begin() const {
        return iterator(this, m_first, 0);
    }

    iterator end() const {
        return iterator(this, invalid, 0);
    }

    void set(const iterator& pos, const posting_type& entry) {
        geodb_assert(pos != end(), "writing to the end iterator");

        set_entry(pos.node, pos.index, entry);
    }

    void push_back(const posting_type& entry) {
        handle_type block = m_last;
        if (block == invalid || get_count(block) == block_entry_count()) {
            {
                // Allocate a new block and link it with the last one.
                handle_type new_block = create_data();

                set_prev(new_block, block);
                if (block != invalid) {
                    set_next(block, new_block);
                }
                block = new_block;
            }

            // Update first and last pointer.
            m_last = block;
            if (m_first == invalid) {
                m_first = block;
            }
        }

        geodb_assert(block != invalid && get_count(block) < block_entry_count(),
                     "block has capacity for one more entry");

        const u32 count = get_count(block);
        set_entry(block, count, entry);
        set_count(block, count + 1);
        ++m_size;
    }

    void pop_back() {
        geodb_assert(m_size > 0, "cannot pop back on an empty list");
        geodb_assert(m_last != invalid, "last block must be valid (invariant)");

        const u32 count = get_count(m_last);
        geodb_assert(count > 0, "data blocks cannot be empty");

        // Simple case: decrement entry count in last block.
        if (count > 1) {
            clear_entry(m_last, count - 1);
            set_count(m_last, count - 1);
            --m_size;
            return;
        }

        // Destroy the last block and unlink it from the list.
        const handle_type block = m_last;
        const handle_type prev = get_prev(block);
        if (prev == invalid) {
            m_first = invalid;
            m_last = invalid;
        } else {
            set_next(prev, invalid);
            m_last = prev;
        }
        --m_size;
        m_blocks.free_block(block);
    }

    void clear() {
        for (handle_type block = m_first; block != invalid; ) {
            handle_type next = get_next(block);
            m_blocks.free_block(block);
            block = next;
        }
        m_first = invalid;
        m_last = invalid;
        m_size = 0;
    }

    size_t size() const {
        return m_size;
    }

public:
    postings_list_blocks_impl(block_collection<block_size>& blocks,
                              block_handle<block_size> base,
                              bool first_time)
        : m_blocks(blocks)
        , m_base(base)
    {
        if (!first_time) {
            load_state();
        }
    }

    ~postings_list_blocks_impl() {
        save_state();
    }

private:
    /// The block collection file is shared among all instances.
    block_collection<block_size>& m_blocks;

    /// Every list has its own base page that never changes.
    /// The base page is used to store the list' size, a reference
    /// to its first and last data block etc.
    const handle_type m_base;

    /// The number of elements in this list.
    size_t m_size = 0;

    /// Pointer to the first data page (or invalid).
    handle_type m_first = invalid;

    /// Pointer to the last data page (or invalid).
    handle_type m_last = invalid;
};

template<typename Posting, size_t block_size>
constexpr block_handle<block_size> postings_list_blocks_impl<Posting, block_size>::invalid;

} // namespace geodb

#endif // GEODB_IRWI_POSTINGS_LIST_BLOCKS_HPP
