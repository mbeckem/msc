#ifndef GEODB_TYPE_TRAITS_HPP
#define GEODB_TYPE_TRAITS_HPP

#include "geodb/common.hpp"

#include <type_traits>

namespace geodb {

// http://en.cppreference.com/w/cpp/types/conjunction
template<class...> struct conjunction : std::true_type { };
template<class B1> struct conjunction<B1> : B1 { };
template<class B1, class... Bn>
struct conjunction<B1, Bn...>
    : std::conditional_t<bool(B1::value), conjunction<Bn...>, B1> {};

template <typename T, template <typename...> class Template>
struct is_specialization_of;

/// True iff `T` is a specialization of `Template`, i.e. `T == Template<Args...>`
/// for some Args.
template <typename T, template <typename...> class Template>
struct is_specialization_of : std::false_type {};

template <template <typename...> class Template, typename... Args>
struct is_specialization_of<Template<Args...>, Template> : std::true_type {};

/// Evaluates to true if all types in `Args` are the same as `T`.
template<typename T, typename... Args>
struct all_of_type : conjunction<std::is_same<T, Args>...> {};

/// Negation of `std::enable_if_t`.
template<bool b, typename T = void>
using disable_if_t = std::enable_if_t<!b, T>;

} // namespace geodb

#endif // GEODB_TYPE_TRAITS_HPP
