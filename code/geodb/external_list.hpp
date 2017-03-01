#ifndef GEODB_EXTERNAL_LIST_HPP
#define GEODB_EXTERNAL_LIST_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"
#include "geodb/irwi/block_handle.hpp"
#include "geodb/utility/raw_stream.hpp"

#include <fmt/format.h>
#include <gsl/span>
#include <tpie/blocks/block_collection_cache.h>

namespace geodb {

/// A list of `Value` objects in external storage.
/// The list allows insertion at the end and random access
/// to stored elements.
///
/// A number of blocks are cached in memory. That number can
/// be modified using the constructor.
///
/// TODO: The last block should not be allowed to leave the cache (?).
template<typename Value, size_t block_size>
class external_list {
private:
    using block_handle_type = block_handle<block_size>;

public:
    using value_type = Value;

    using size_type = u64;

private:
    static_assert(sizeof(value_type) <= block_size,
                  "Value is greater than block_size");
    static_assert(std::is_trivially_copyable<value_type>::value,
                  "Value must be trivially copyable");

    static constexpr size_type block_capacity() {
        return block_size / sizeof(value_type);
    }

public:
    /// Open a list with the given path.
    ///
    /// \param path
    ///     The path to a directory on disk.
    ///     The directory must exist, but may be empty.
    ///
    /// \param cache_blocks
    ///     The number of blocks cached in main memory.
    external_list(const fs::path& path, size_t cache_blocks)
        : m_path(path)
        , m_blocks((path / "list.blocks").string(), block_size, std::max(cache_blocks, (size_t) 1), true)
    {
        raw_stream rf;
        if (rf.try_open(state_path())) {
            size_t file_block_size;
            rf.read(file_block_size);
            if (file_block_size != block_size) {
                throw std::invalid_argument(fmt::format("Invalid block size. Expected {} but found {}.",
                                                        block_size, file_block_size));
            }

            rf.read(m_block_count);
            rf.read(m_value_count);
            rf.read(m_block_value_count);
            rf.read(m_current_block);
        }
    }

    ~external_list() {
        raw_stream rf;
        rf.open_new(state_path());
        rf.write(block_size);

        rf.write(m_block_count);
        rf.write(m_value_count);
        rf.write(m_block_value_count);
        rf.write(m_current_block);
    }

    external_list(const external_list&) = delete;

    external_list& operator=(const external_list&) = delete;

    /// Returns the total number of allocated blocks.
    size_type blocks() const { return m_block_count; }

    /// Returns the number of values in this list.
    size_type size() const { return m_value_count; }

    /// Returns the value at the given index.
    /// \pre `index < size()`.
    value_type operator[](size_type index) {
        geodb_assert(index < m_value_count, "index out of bounds");

        size_type block_index = index / block_capacity();
        size_type index_in_block = index % block_capacity();
        return get(block_index, index_in_block);
    }

    /// Appends a value.
    void append(const value_type& value) {
        if (m_block_count == 0 || m_block_value_count == block_capacity()) {
            next_block();
            m_block_value_count = 0;
        }

        set(m_current_block, m_block_value_count, value);
        ++m_block_value_count;
        ++m_value_count;
    }

private:
    fs::path state_path() const {
        return m_path / "list.state";
    }

    void next_block() {
        block_handle_type handle = m_blocks.get_free_block();
        geodb_assert(handle.index() == m_block_count,
                     "blocks are allocated sequentially and are never freed.");

        ++m_block_count;
        m_current_block = handle;
    }

    void set(block_handle_type block, size_type index, const value_type& value) {
        geodb_assert(index < block_capacity(), "index out of bounds");

        auto data = read_block(block);
        size_type offset = index * sizeof(value_type);

        memcpy(&data[offset], std::addressof(value), sizeof(value_type));
        write_block(block);
    }

    value_type get(block_handle_type block, size_type index) const {
        geodb_assert(index < block_capacity(), "index out of bounds");

        auto data = read_block(block);
        size_type offset = index * sizeof(value_type);
        value_type result;

        memcpy(std::addressof(result), &data[offset], sizeof(value_type));
        return result;
    }

    gsl::span<char, block_size> read_block(block_handle_type handle) const {
        geodb_assert(handle.index() < m_block_count, "block index out of bounds");

        tpie::blocks::block* block = m_blocks.read_block(handle);
        return { block->get(), block_size };
    }

    void write_block(block_handle_type handle) {
        geodb_assert(handle.index() < m_block_count, "block index out of bounds");

        m_blocks.write_block(handle);
    }

private:
    /// Path to the block storage on disk.
    fs::path m_path;

    /// Number of blocks allocated by this instance.
    size_type m_block_count = 0;

    /// Number of values stored in this instance.
    size_type m_value_count = 0;

    /// Number of items in the current block.
    size_type m_block_value_count = 0;

    /// Points to the current block (the one we're appending to).
    block_handle_type m_current_block;

    /// Block storage on disk + cache.
    mutable tpie::blocks::block_collection_cache m_blocks;
};

}

#endif // GEODB_EXTERNAL_LIST_HPP