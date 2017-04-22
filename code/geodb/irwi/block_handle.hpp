#ifndef GEODB_IRWI_BLOCK_HANDLE_HPP
#define GEODB_IRWI_BLOCK_HANDLE_HPP

#include "geodb/common.hpp"

#include <tpie/blocks/block.h>

/// \file
/// A handle to a block on disk, used in conjunction with `block_collection`.

namespace geodb {

/// A handle to a block with a size determined at compile time.
/// This saves us space on disk because the size does not have
/// to be stored in the file.
template<size_t block_size>
struct block_handle {
    // for serialization support.
    static const bool is_trivially_serializable = true;

private:
    u64 m_index = 0;

public:
    constexpr block_handle() {}

    /// Construct a handle from a raw block index.
    constexpr block_handle(u64 index): m_index(index) {}

    /// Block handle must refer to a block of size `block_size`
    /// and a valid position divisible by `block_size`.
    constexpr block_handle(tpie::blocks::block_handle h)
        : m_index(h.position / block_size)
    {
        geodb_assert(h.position % block_size == 0,
                     "position must be a multiple of block_size");
        geodb_assert(h.size == block_size, "block must have the correct size");
    }

    /// Returns the index of the block.
    constexpr u64 index() const {
        return m_index;
    }

    constexpr bool operator==(const block_handle& other) const {
        return m_index == other.m_index;
    }

    constexpr bool operator!=(const block_handle& other) const {
        return m_index != other.m_index;
    }

    operator tpie::blocks::block_handle() const {
        return { m_index * block_size, block_size };
    }

    // for boost::hash.
    friend size_t hash_value(const block_handle& handle) {
        return std::hash<u64>()(handle.index());
    }
};

} // namespace geodb

namespace std {

template<size_t block_size>
struct hash<geodb::block_handle<block_size>> {
    size_t operator()(const geodb::block_handle<block_size>& handle) const {
        return std::hash<geodb::u64>()(handle.index());
    }
};

} // namespace std

#endif // GEODB_IRWI_BLOCK_HANDLE_HPP
