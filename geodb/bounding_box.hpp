#ifndef BOUNDING_BOX_HPP
#define BOUNDING_BOX_HPP

#include "geodb/common.hpp"
#include "geodb/interval.hpp"
#include "geodb/point.hpp"

namespace geodb {

/// A 3-dimensional rectangle that encompasses spatio-temporal objects.
/// Represented by two points (min, max).
struct bounding_box {
public:
    /// Construct an empty bounding box.
    bounding_box() = default;

    /// Construct a bounding box from a pair of points.
    bounding_box(point min, point max)
        : m_min(min)
        , m_max(max)
    {
        geodb_assert(point::less_eq(min, max), "min is not less than max");
    }

    /// Returns the minimum point of the bounding box.
    const point& min() const { return m_min; }

    /// Returns the maximum point of the bounding box.
    const point& max() const { return m_max; }

    /// Returns the center point of the bounding box.
    point center() const {
        point result;
        result.x() = (m_max.x() - m_min.x()) / 2;
        result.y() = (m_max.y() - m_min.y()) / 2;
        result.t() = (m_max.t() - m_min.t()) / 2;
        return result;
    }

    /// Returns true if this bounding box fully contains `other`.
    bool contains(const bounding_box& other) const {
        return point::less_eq(m_min, other.m_min) && point::less_eq(other.m_max, m_max);
    }

    /// Returns true if this bounding box has a non-empty intersection
    /// with the other bounding box.
    bool intersects(const bounding_box& other) const {
        return      (min().x() <= other.max().x() && max().x() >= other.min().x())
                 && (min().y() <= other.max().y() && max().y() >= other.min().y())
                 && (min().t() <= other.max().t() && max().t() >= other.min().t());
    }

    /// Returns a minimum bounding box that contains
    /// both \p *this and \p other.
    bounding_box extend(const bounding_box& other) const {
        return { point::min(m_min, other.m_min), point::max(m_max, other.m_max) };
    }

    /// Returns the size of this box.
    float size() const {
        point vec = m_max - m_min;
        return float(vec.x()) * float(vec.y()) * float(vec.t());
    }

private:
    friend bool operator==(const bounding_box& a, const bounding_box& b) {
        return a.m_min == b.m_min && a.m_max == b.m_max;
    }

    friend bool operator!=(const bounding_box& a, const bounding_box& b) {
        return a.m_min != b.m_min || a.m_max != b.m_max;
    }

    friend std::ostream& operator<<(std::ostream& o, const bounding_box& b) {
        return o << "{min: " << b.m_min << ", max: " << b.m_max << "}";
    }

private:
    point m_min;
    point m_max;
};

} // namespace geodb

#endif // BOUNDING_BOX_HPP
