#ifndef FILE_ALLOCATOR_HPP
#define FILE_ALLOCATOR_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"
#include "geodb/utility/id_allocator.hpp"

#include <boost/noncopyable.hpp>

namespace geodb {

/// An allocator for files in a user provided directory.
///
/// Allocated files are referred to using unique numeric identifiers.
/// Files are created using \ref alloc() and destroyed using \ref free.
///
/// \ref path() returns the real path on disk for an active identifier.
template<typename Derived, typename IdType>
class file_allocator_base : boost::noncopyable {
    using id_allocator_type = id_allocator<IdType>;

public:
    using value_type = typename id_allocator_type::value_type;

public:
    /// Creates an allocator for the given directory.
    /// The directory must already exist. The directory may contain
    /// data from a previous allocator, in which case the state
    /// of the allocator will be restored.
    file_allocator_base(fs::path directory, std::string suffix)
        : m_directory(std::move(directory))
        , m_suffix(std::move(suffix))
        , m_ids((m_directory / "allocator.state").string())
    {

    }

    /// Allocates a new file id.
    value_type alloc() {
        value_type id = m_ids.alloc();
        static_cast<Derived*>(this)->create(path(id));
        return id;
    }

    /// Frees the given id. If an associated file exists on disk,
    /// it will be removed
    void free(value_type id) {
        if (id) {
            m_ids.free(id);
            static_cast<Derived*>(this)->remove(path(id));
        }
    }

    /// Returns the filesystem path for some id
    /// obtained by calling \ref alloc.
    fs::path path(value_type id) const {
        geodb_assert(id != 0, "id must point to a valid page");
        std::string filename = std::to_string(id) + m_suffix;
        return m_directory / filename;
    }

private:
    fs::path m_directory;
    std::string m_suffix;
    id_allocator_type m_ids;
};

/// A allocator that allocates plain files on disk.
/// Files will not be created once allocated. Freeing removes the file
/// if it exists.
template<typename IdType>
class file_allocator : public file_allocator_base<file_allocator<IdType>, IdType> {
public:
    /// \copydoc file_allocator_base::file_allocator_base(fs::path, std::string)
    file_allocator(fs::path directory, std::string suffix = ".node")
        : file_allocator::file_allocator_base(std::move(directory), std::move(suffix))
    {}

private:
    friend class file_allocator_base<file_allocator<IdType>, IdType>;

    // Does nothing.
    void create(const fs::path&) {}

    void remove(const fs::path& p) {
        // Missing file is not an error.
        fs::remove(p);
    }
};

/// A allocator that allocates directories on disk.
/// Directories will be created at allocation time and recursively removed when freed.
template<typename IdType>
class directory_allocator : public file_allocator_base<directory_allocator<IdType>, IdType> {
public:
    /// \copydoc file_allocator_base::file_allocator_base(fs::path, std::string)
    directory_allocator(fs::path directory, std::string suffix = "")
        : directory_allocator::file_allocator_base(std::move(directory), std::move(suffix))
    {}

private:
    friend class file_allocator_base<directory_allocator<IdType>, IdType>;

    void create(const fs::path& p) {
        fs::create_directory(p);
    }

    void remove(const fs::path& p) {
        fs::remove_all(p);
    }
};

} // namespace geodb

#endif // FILE_ALLOCATOR_HPP
