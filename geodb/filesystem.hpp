#ifndef GEODB_FILESYSTEM_HPP
#define GEODB_FILESYSTEM_HPP

#include "geodb/common.hpp"

#include <boost/filesystem.hpp>

namespace geodb {

namespace fs = boost::filesystem;

/// Creates the directory `p` and all required parents.
/// Existing directories are not an error.
/// \return Returns the path.
inline fs::path ensure_directory(fs::path p) {
    fs::create_directories(p);
    return p;
}

} // namespace geodb

#endif // GEODB_FILESYSTEM_HPP
