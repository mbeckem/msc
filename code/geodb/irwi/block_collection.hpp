#ifndef GEODB_IRWI_BLOCK_COLLECTION_HPP
#define GEODB_IRWI_BLOCK_COLLECTION_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"
#include "geodb/irwi/block_handle.hpp"

#include <tpie/blocks/block_collection_cache.h>

/// \file
/// A collection of blocks on disk.

namespace geodb {

/// A block file that hands out blocks of the given BlockSize.
/// Free blocks are managed by a free list.
/// A number of blocks can be cached in memory.
template<size_t BlockSize>
class block_collection {
public:
    using handle_type = block_handle<BlockSize>;

public:
    /// A block collection at the given file system location
    /// with the specified cache size.
    block_collection(const fs::path& path, size_t max_cache = 32, bool read_only = false)
        : m_blocks(path.string(), BlockSize, std::max(max_cache, size_t(4)), !read_only)
    {}

    /// Allocates a new block.
    handle_type get_free_block() {
        handle_type handle = m_blocks.get_free_block();
        return handle;
    }

    /// Frees the given block.
    /// Free'd blocks are reused when a new block is allocated.
    void free_block(handle_type handle) {
        m_blocks.free_block(handle);
    }

    /// Read the data at the given block index.
    tpie::blocks::block* read_block(handle_type handle) {
        return m_blocks.read_block(handle);
    }

    /// Mark the given block as "dirty", causing any changes
    /// to be written to disk eventually.
    void write_block(handle_type handle) {
        m_blocks.write_block(handle);
    }

    /// For compatibility.
    operator tpie::blocks::block_collection_cache& () {
        return m_blocks;
    }

    static constexpr size_t block_size() { return BlockSize; }

private:
    tpie::blocks::block_collection_cache m_blocks;
};

} // namespace geodb

#endif // GEODB_IRWI_BLOCK_COLLECTION_HPP
