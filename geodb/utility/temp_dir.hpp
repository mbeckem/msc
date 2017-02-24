#ifndef GEODB_UTILITY_TEMP_DIR_HPP
#define GEODB_UTILITY_TEMP_DIR_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"

#include <boost/noncopyable.hpp>
#include <tpie/tempname.h>

namespace geodb {

/// A temporary directory on disk. The directory will be deleted when
/// this object goes out of scope.
class temp_dir : boost::noncopyable {
public:
    /// Create a new temporary directory with a unique name.
    /// \param id
    ///     This string will become part of the directory name.
    temp_dir(const std::string& id = ""): m_path(tpie::tempname::tpie_dir_name(id)) {
        fs::create_directories(m_path);
    }

    /// The destructors recursively removes the entire directory.
    ~temp_dir() {
        if (fs::exists(m_path)) {
            fs::remove_all(m_path);
        }
    }

    /// Returns the filesystem path of this directory.
    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

} // namespace geodb

#endif // GEODB_UTILITY_TEMP_DIR_HPP
