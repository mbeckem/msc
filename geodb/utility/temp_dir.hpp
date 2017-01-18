#ifndef UTILITY_TEMP_DIR_HPP
#define UTILITY_TEMP_DIR_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"

#include <boost/noncopyable.hpp>
#include <tpie/tempname.h>

namespace geodb {

/// A temporary directory on disk. The directory will be deleted when
/// this object goes out of scope.
class temp_dir : boost::noncopyable {
public:
    temp_dir(): m_path(tpie::tempname::tpie_dir_name()) {
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

#endif // UTILITY_TEMP_DIR_HPP
