#ifndef GEODB_UTILITY_FUNCTION_UTILS_HPP
#define GEODB_UTILITY_FUNCTION_UTILS_HPP

#include "geodb/common.hpp"

/// \file
/// Contains generic functional utilities.

namespace geodb {

/// Calls a function a given number of times.
template<typename Integer, typename Fn>
void repeat(Integer count, Fn&& fn) {
    for (Integer i = 0; i < count; ++i) {
        fn();
    }
}

} // namespace geodb

#endif // GEODB_UTILITY_FUNCTION_UTILS_HPP
