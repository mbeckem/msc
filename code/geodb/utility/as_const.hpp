#ifndef GEODB_UTILITY_AS_CONST_HPP
#define GEODB_UTILITY_AS_CONST_HPP

#include "geodb/common.hpp"
#include "geodb/type_traits.hpp"

/// \file
/// Const casting utilities.

namespace geodb {

template<typename T>
const T* as_const(const T* ptr) {
    return ptr;
}

template<typename T, disable_if_t<std::is_pointer<T>::value>* = nullptr>
const T& as_const(const T& ref) {
    return ref;
}

} // namespace geodb

#endif // GEODB_UTILITY_AS_CONST_HPP
