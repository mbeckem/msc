#ifndef GEODB_UTILITY_TUPLE_UTILS_HPP
#define GEODB_UTILITY_TUPLE_UTILS_HPP

#include "geodb/common.hpp"
#include "geodb/type_traits.hpp"

#include <tuple>
#include <type_traits>
#include <utility>

namespace geodb {

namespace detail {

template <typename Tuple, typename F, size_t... I>
void for_each_impl(Tuple&& t, F&& f, std::index_sequence<I...>)
{
    // Put results in an array to force order of evaluation.
    int ignored[] = {
        (static_cast<void>(f(std::get<I>(t))), 0)...
    };
    unused(ignored);
}

template<typename F, size_t... I>
void enumerate_impl(F&& f, std::index_sequence<I...>) {
    int ignored[] = {
        (static_cast<void>(f(std::integral_constant<size_t, I>())), 0)...
    };
    unused(ignored);
}

}  // namespace detail


/// Iterate over the elements of a tuple.
/// The provided function object `f` will be called for every element
/// in the tuple, in sequential order.
template <typename Tuple, typename F,
          std::enable_if_t<is_specialization_of<Tuple, std::tuple>::value>* = nullptr>
void for_each(Tuple&& t, F&& f)
{
    return detail::for_each_impl(
        std::forward<F>(f), std::forward<Tuple>(t),
        std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>());
}

/// Calls the provided function object exactly `size` times.
template<size_t size, typename F>
void enumerate(F&& f) {
    return detail::enumerate_impl(
        std::forward<F>(f), std::make_index_sequence<size>());
}

} // namespace geodb

#endif // GEODB_UTILITY_TUPLE_UTILS_HPP
