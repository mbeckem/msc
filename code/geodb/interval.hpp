#ifndef GEODB_INTERVAL_HPP
#define GEODB_INTERVAL_HPP

#include "geodb/common.hpp"

#include <tpie/serialization2.h>

#include <array>
#include <algorithm>
#include <ostream>

/// \file
/// Contains a basic interval type.

namespace geodb {

/// An interval `[begin, end]` of integers.
/// Intervals contain both their beginning and their end point.
/// There are no empty intervals.
template<typename T>
class interval {
public:
    using value_type = T;

public:
    /// Equivalent to `interval(0)`.
    interval(): interval(0) {}

    /// Constructs an interval of size 1 representing `point`.
    interval(T point): interval(point, point) {}

    /// Constructs a new interval with the given `begin` and `end`.
    /// \pre `begin <= end`.
    interval(T begin, T end): m_begin(begin), m_end(end) {
        geodb_assert(begin <= end, "invalid interval");
    }

    /// Returns the begin of the interval.
    T begin() const { return m_begin; }

    /// Returns the end point of the interval.
    T end() const { return m_end; }

    /// Returns the distance from this interval to the given point.
    T distance_to(T point) const {
        if (point < m_begin) {
            return m_begin - point;
        } else if (point > m_end) {
            return point - m_end;
        } else {
            return 0; // contains(point).
        }
    }

    /// Returns true iff this interval contains the given point.
    bool contains(T point) const {
        return point >= m_begin && point <= m_end;
    }

    /// Returns true if this interval contains the other interval.
    bool contains(const interval& other) const {
        return other.m_begin >= m_begin && other.m_end <= m_end;
    }

    /// Returns true if this interval and `other` share at least
    /// one common integer.
    bool overlaps(const interval& other) const {
        return other.m_end >= m_begin && other.m_begin <= m_end;
    }

private:
    template<typename Dst>
    friend void serialize(Dst& dst, const interval& a) {
        using tpie::serialize;
        serialize(dst, a.m_begin);
        serialize(dst, a.m_end);
    }

    template<typename Src>
    friend void unserialize(Src& src, interval& a) {
        using tpie::unserialize;
        unserialize(src, a.m_begin);
        unserialize(src, a.m_end);
        geodb_assert(a.m_begin <= a.m_end, "invalid interval");
    }

private:
    T m_begin;
    T m_end;
};

template<typename T>
bool operator==(const interval<T>& a, const interval<T>& b) {
    return a.begin() == b.begin() && a.end() == b.end();
}

template<typename T>
bool operator==(const interval<T>& a, const T& b) {
    return a.begin() == b && a.end() == b;
}

template<typename T>
bool operator==(const T& a, const interval<T>& b) {
    return b.begin() == a && b.end() == a;
}

template<typename T>
bool operator!=(const interval<T>& a, const interval<T>& b) {
    return a.begin() != b.begin() || a.end() != b.end();
}

template<typename T>
bool operator!=(const interval<T>& a, const T& b) {
    return a.begin() != b || a.end() != b;
}

template<typename T>
bool operator!=(const T& a, const interval<T>& b) {
    return b.begin() != a || b.end() != a;
}

template<typename T>
std::ostream& operator<<(std::ostream& o, const interval<T>& i) {
    if (i.begin() == i.end()) {
        return o << "[" << i.begin() << "]";
    }
    return o << "[" << i.begin() << "-" << i.end() << "]";
}

} // namespace geodb

#endif // GEODB_INTERVAL_HPP
