#ifndef GEODB_UTILITY_TEMP_DIR_HPP
#define GEODB_UTILITY_TEMP_DIR_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"

#include <tpie/tempname.h>

#include <memory>

namespace geodb {

/// A temporary directory on disk.
///
/// Copying a temp_dir will increase the reference count of the directory on disk.///
/// The directory will be deleted when the last reference goes out of scope.
class temp_dir {
private:
    // No attempt is made to count the size of the directory,
    // in constrast to tpie::tmpfile;
    struct inner {
        fs::path path;

        inner(fs::path p)
            : path(std::move(p))
        {
            fs::create_directories(path);
        }

        ~inner() {
            if (fs::exists(path)) {
                fs::remove_all(path);
            }
        }
    };

public:
    /// Create a new temporary directory with a unique name.
    /// \param id
    ///     This string will become part of the directory name.
    temp_dir(const std::string& id = "")
        : m_inner(std::make_shared<inner>(tpie::tempname::tpie_dir_name(id)))
    {}

    /// Returns the filesystem path of this directory.
    const fs::path& path() const { return m_inner->path; }

private:
    std::shared_ptr<inner> m_inner;
};

} // namespace geodb

#endif // GEODB_UTILITY_TEMP_DIR_HPP
