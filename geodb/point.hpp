#ifndef POINT_HPP
#define POINT_HPP

#include "geodb/common.hpp"
#include "geodb/type_traits.hpp"
#include "geodb/utility/tuple_utils.hpp"

#include <tpie/serialization2.h>

#include <algorithm>
#include <ostream>
#include <tuple>

namespace geodb {

namespace detail {

/// CRTP base class for point implementations.
/// Every coordinate can have a different type.
///
/// The derived class must implement the static functions
/// `Derived::get_member(std::integral_constant<size_t, I>)` for all I in 0,...,Size-1 .
template<typename Derived, size_t Size>
class point_base {
public:
    static constexpr size_t size() { return Size; }

    static_assert(size() >= 1, "Must at least have a dimension of 1.");

public:
    /// Access element at position `Index`.
    template<size_t Index>
    auto& get() {
        static_assert(Index < size(), "index out of bounds.");
        return get(std::integral_constant<size_t, Index>());
    }

    /// Access element at position `Index`.
    template<size_t Index>
    const auto& get() const {
        static_assert(Index < size(), "index out of bounds.");
        return get(std::integral_constant<size_t, Index>());
    }

    template<size_t Index>
    auto& get(std::integral_constant<size_t, Index> index) {
        static_assert(Index < size(), "index out of bounds.");
        return Derived::get_member(derived(), index);
    }

    template<size_t Index>
    const auto& get(std::integral_constant<size_t, Index> index) const {
        static_assert(Index < size(), "index out of bounds.");
        return Derived::get_member(derived(), index);
    }

    friend bool operator==(const Derived& a, const Derived& b) {
        return a.as_tuple() == b.as_tuple();
    }

    friend bool operator!=(const Derived& a, const Derived& b) {
        return a.as_tuple() != b.as_tuple();
    }

    /// Creates a new point by performing element-wise addition of `a` and `b`.
    friend Derived operator+(const Derived& a, const Derived& b) {
        Derived r;
        enumerate<size()>([&](auto i) {
            r.get(i) = a.get(i) + b.get(i);
        });
        return r;
    }

    /// Creates a new point by performing element-wise subtraction of `a` and `b`.
    friend Derived operator-(const Derived& a, const Derived& b) {
        Derived r;
        enumerate<size()>([&](auto i) {
            r.get(i) = a.get(i) - b.get(i);
        });
        return r;
    }

    /// Element-wise minumum operation.
    static Derived min(const Derived& a, const Derived& b) {
        Derived m;
        enumerate<size()>([&](auto i) {
            m.get(i) = std::min(a.get(i), b.get(i));
        });
        return m;
    }

    /// Element-wise maximum operation.
    static Derived max(const Derived& a, const Derived& b) {
        Derived m;
        enumerate<size()>([&](auto i) {
            m.get(i) = std::max(a.get(i), b.get(i));
        });
        return m;
    }

    /// Returns true iff all coordinates of p1 are lesser than
    /// or equal to their counterparts in p2.
    /// In other words, this is true if `a` is "bottom-left" from `b`.
    static bool less_eq(const Derived& a, const Derived& b) {
        bool result = true;
        enumerate<size()>([&](auto i) {
            result = result && a.get(i) <= b.get(i);
        });
        return result;
    }

    friend std::ostream& operator<<(std::ostream& o, const Derived& d) {
        o << "(";
        enumerate<size()>([&](auto i) {
            if (i != 0) {
                o << ", ";
            }
            o << d.get(i);
        });
        o << ")";
        return o;
    }

private:
    const Derived& derived() const { return static_cast<const Derived&>(*this); }
    Derived& derived() { return static_cast<Derived&>(*this); }

    template<size_t... I>
    auto as_tuple_impl(std::index_sequence<I...>) const {
        return std::tie(get<I>()...);
    }

    auto as_tuple() const {
        return as_tuple_impl(std::make_index_sequence<size()>());
    }
};

} // namespace detail

// Float for x and y coordinates.
using spatial_type = float;

// TODO: What type for time?
using time_type = u32;

/// A 3-dimensional point with 2 spatial dimensions (x, y)
/// and one temporal dimension (t).
/// Note that the type used for time is distinct from the type of
/// the spatial coordinates.
class point : public detail::point_base<point, 3> {
public:
    using x_type = spatial_type;
    using y_type = spatial_type;
    using t_type = time_type;

public:
    point() = default;

    point(x_type x, y_type y, t_type t)
        : m_t(t), m_y(y), m_x(x)
    {}

    const x_type& x() const { return m_x; }
    const y_type& y() const { return m_y; }
    const t_type& t() const { return m_t; }

    x_type& x() { return m_x; }
    y_type& y() { return m_y; }
    t_type& t() { return m_t; }

private:
    friend class point_base<point, 3>;

    template<typename T>
    static constexpr bool is_point() {
        return std::is_same<std::decay_t<T>, point>::value;
    }

    template<typename Point>
    static decltype(auto) get_member(Point&& p, std::integral_constant<size_t, 0>) { return p.x(); }

    template<typename Point>
    static decltype(auto) get_member(Point&& p, std::integral_constant<size_t, 1>) { return p.y(); }

    template<typename Point>
    static decltype(auto) get_member(Point&& p, std::integral_constant<size_t, 2>) { return p.t(); }

private:
    template<typename Dst>
    friend void serialize(Dst& dst, const point& p) {
        using tpie::serialize;
        serialize(dst, p.x());
        serialize(dst, p.y());
        serialize(dst, p.t());
    }

    template<typename Src>
    friend void unserialize(Src& src, point& p) {
        using tpie::unserialize;
        unserialize(src, p.x());
        unserialize(src, p.y());
        unserialize(src, p.t());
    }

private:
    // FIXME: Order is reversed to keep binary compat with
    // the previous std::tuple implementation.
    t_type m_t = 0;
    y_type m_y = 0;
    x_type m_x = 0;
};

} // namespace geodb

#endif // POINT_HPP
