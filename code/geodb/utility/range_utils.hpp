#ifndef GEODB_UTILITY_RANGE_UTILS_HPP
#define GEODB_UTILITY_RANGE_UTILS_HPP

#include "geodb/common.hpp"

#include <boost/range/functions.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/optional.hpp>

#include <type_traits>

namespace geodb {

/// Returns a function that, when given an instance of type T,
/// returns a reference to an attribute specified by the ptr.
///
/// Example:
///
///     auto get_x = map_member(&my_type::x);
///
///     my_type t;
///     get_x(t); // Returns t.x
template<typename T, typename V>
auto map_member(V T::*ptr) {
    return [=](auto&& instance) -> decltype(auto) {
        using dt = decltype(instance);
        return std::forward<dt>(instance).*ptr;
    };
}

/// Returns a function that, when given an instance of type T,
/// returns the result of calling the nullary member function of T.
template<typename T, typename V>
auto map_member(V (T::*ptr)()) {
    return [=](auto&& instance) -> decltype(auto) {
        using dt = decltype(instance);
        return (std::forward<dt>(instance).*ptr)();
    };
}

namespace detail {

// A type that wraps a function object to make it both default constructible
// and assignable. Lambdas, for example, do not support these operations.
template<typename Func>
class func_wrapper {
public:
    func_wrapper(): func() {}

    func_wrapper(const func_wrapper& other): func(other.func) {}

    func_wrapper(func_wrapper&& other): func(std::move(other.func)) {}

    explicit func_wrapper(const Func& f): func(f) {}

    explicit func_wrapper(Func&& f): func(std::move(f)) {}

    func_wrapper& operator=(const func_wrapper& other) {
        // Emplace would invalidate *other.func
        if (this != &other) {
            if (other.func) {
                func.emplace(*other.func);
            } else {
                func = boost::none;
            }
        }
        return *this;
    }

    func_wrapper& operator=(func_wrapper&& other) {
        // Emplace would invalidate *other.func
        if (this != &other) {
            if (other.func) {
                func.emplace(std::move(*other.func));
            } else {
                func = boost::none;
            }
        }
        return *this;
    }

    template<typename... Args>
    decltype(auto) operator()(Args&&... args) const {
        geodb_assert(func, "called for singular function instance");
        return (*func)(std::forward<Args>(args)...);
    }

private:
    boost::optional<Func> func;
};

} // namespace detail

/// A version of boost::adaptors::transformed that works with lambdas.
template<typename Func>
auto transformed(Func f) {
    using func_t = std::conditional_t<
        !std::is_default_constructible<Func>::value || !std::is_copy_assignable<Func>::value,
        detail::func_wrapper<Func>,
        Func
    >;
    return boost::adaptors::transformed(func_t(std::move(f)));
}

template<typename MemberPointer>
auto transformed_member(MemberPointer ptr) {
    return transformed(map_member(ptr));
}

template<typename Iter>
auto iterate(Iter begin, Iter end) {
    return boost::make_iterator_range(begin, end);
}

} // namespace geodb

#endif // GEODB_UTILITY_RANGE_UTILS_HPP
