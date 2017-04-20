#ifndef GEODB_UTILITY_NOOP_HPP
#define GEODB_UTILITY_NOOP_HPP

#include "geodb/common.hpp"

/// \file
/// A function object that does nothing.

namespace geodb {

/// A function object that accept any arguments and does nothing.
class noop {
public:
    template<typename... Args>
    void operator()(Args&&... args) const {
        unused(std::forward<Args>(args)...);
    }
};

} // namespace geodb

#endif // GEODB_UTILITY_NOOP_HPP
