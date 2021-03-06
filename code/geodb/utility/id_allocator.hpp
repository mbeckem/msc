#ifndef GEODB_UTILITY_ID_ALLOCATOR_HPP
#define GEODB_UTILITY_ID_ALLOCATOR_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"

#include <string>
#include <limits>
#include <stdexcept>

#include <boost/noncopyable.hpp>
#include <tpie/stack.h>

/// \file
/// An allocator that hands out free ids.

namespace geodb {

/// An allocator for unique numeric IDs.
/// The class hands out numbers by incrementing
/// an internal counter (starting with the ID 1).
///
/// Freed IDs are stored in a list for reuse.
///
/// \tparam IdType Some numeric type.
template<typename IdType>
class id_allocator : boost::noncopyable {
public:
    using value_type = IdType;

    /// Construct a new allocator.
    ///
    /// \param path
    ///     The location of the allocator on disk.
    ///     If the file already exists, the previous state
    ///     of the allocator will be restored.
    id_allocator(const fs::path& path)
        : m_free(path.string())
    {
        if (!m_free.empty()) {
            // Read m_count written by the destructor.
            m_count = m_free.pop();
        }
    }

    ~id_allocator() {
        // Save the count to the top of the stack.
        m_free.push(m_count);
    }

    /// Allocates a unique identifier.
    value_type alloc() {
        if (!m_free.empty()) {
            return m_free.pop();
        }
        if (m_count == std::numeric_limits<value_type>::max()) {
            throw std::overflow_error("id overflow");
        }
        return ++m_count;
    }

    /// Free an identifier that was obtained by calling \ref alloc().
    void free(value_type id) {
        geodb_assert(id <= m_count, "id was not obtained through this instance");
        if (id == 0) {
            return;
        }
        m_free.push(id);
    }

    /// Returns the number of used IDs.
    /// The ids [1, count] are currently in use.
    value_type count() const {
        return m_count;
    }

    /// Resets the state of this allocator.
    /// All existing allocated ids are freed.
    void reset() {
        m_count = 0;
        m_free.clear();
    }

private:
    /// Number of allocated ids. The next id is always m_count + 1,
    /// unless there is a freed id which can be reused instead.
    value_type m_count = 0;

    /// Stack of freed ids. These will be reused.
    tpie::stack<value_type> m_free;
};

} // namespace geodb

#endif // GEODB_UTILITY_ID_ALLOCATOR_HPP
