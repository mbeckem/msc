#ifndef GEODB_IRWI_BLOCK_HANDLE_HPP
#define GEODB_IRWI_BLOCK_HANDLE_HPP

#include "geodb/common.hpp"

#include <tpie/blocks/block.h>

namespace geodb {

/// block sizes are always the same.
/// the IRWI block_handle class wastes space by
/// additionaly storing the block size.
template<size_t block_size>
struct block_handle {
    // for serialization support.
    static const bool is_trivially_serializable = true;

private:
    u64 m_index = 0;

public:
    block_handle() {}

    /// Construct a handle from a raw block index.
    block_handle(u64 index): m_index(index) {}

    /// Block handle must refer to a block of size `block_size`
    /// and a valid position divisible by `block_size`.
    block_handle(tpie::blocks::block_handle h)
        : m_index(h.position / block_size)
    {
        geodb_assert(h.position % block_size == 0,
                     "position must be a multiple of block_size");
        geodb_assert(h.size == block_size, "block must have the correct size");
    }

    u64 index() const {
        return m_index;
    }

    bool operator==(const block_handle& other) const {
        return m_index == other.m_index;
    }

    bool operator!=(const block_handle& other) const {
        return m_index != other.m_index;
    }

    operator tpie::blocks::block_handle() const {
        return { m_index * block_size, block_size };
    }
};

} // namespace geodb

#endif // GEODB_IRWI_BLOCK_HANDLE_HPP
