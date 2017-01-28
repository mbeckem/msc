#ifndef POINT_HPP
#define POINT_HPP

#include "geodb/common.hpp"
#include "geodb/utility/tuple_utils.hpp"

#include <tpie/serialization2.h>

#include <algorithm>
#include <ostream>
#include <tuple>

namespace geodb {

namespace detail {

/// CRTP base class for point implementations.
/// Every coordinate can have a different type.
template<typename Derived, typename... Coordinates>
class point_base {
public:
    static constexpr size_t size() { return sizeof...(Coordinates); }

    static_assert(size() >= 1, "Must at least have a dimension of 1.");

public:
    point_base(): m_data() {}
    point_base(Coordinates... coords): m_data(coords...) {}

    /// Access element at position `Index`.
    template<size_t Index>
    auto& get() {
        static_assert(Index < size(), "index out of bounds.");
        return std::get<Index>(m_data);
    }

    /// Access element at position `Index`.
    template<size_t Index>
    const auto& get() const {
        static_assert(Index < size(), "index out of bounds.");
        return std::get<Index>(m_data);
    }

    /// Access element at position `Index`.
    template<size_t Index>
    auto& get(std::integral_constant<size_t, Index>) { return get<Index>(); }

    /// Access element at position `Index`.
    template<size_t Index>
    const auto& get(std::integral_constant<size_t, Index>) const { return get<Index>(); }

    friend bool operator==(const Derived& a, const Derived& b) {
        return a.m_data == b.m_data;
    }

    friend bool operator!=(const Derived& a, const Derived& b) {
        return a.m_data != b.m_data;
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
    std::tuple<Coordinates...> m_data;
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
class point : public detail::point_base<point, spatial_type, spatial_type, time_type> {
public:
    using point::point_base::point_base;

    using x_type = spatial_type;
    using y_type = spatial_type;
    using t_type = time_type;

    const x_type& x() const { return get<0>(); }
    const y_type& y() const { return get<1>(); }
    const t_type& t() const { return get<2>(); }

    x_type& x() { return get<0>(); }
    y_type& y() { return get<1>(); }
    t_type& t() { return get<2>(); }

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
};

} // namespace geodb

#endif // POINT_HPP
