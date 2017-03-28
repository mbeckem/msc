#ifndef GEODB_BOUNDING_BOX_HPP
#define GEODB_BOUNDING_BOX_HPP

#include "geodb/common.hpp"
#include "geodb/interval.hpp"
#include "geodb/rectangle.hpp"
#include "geodb/vector.hpp"

namespace geodb {

/// A 3-dimensional rectangle that encompasses spatio-temporal objects.
/// Represented by two points (min, max).
class bounding_box : public rect_base<vector3, bounding_box> {
public:
    using rect_base::rect_base;

    /// Returns the center point of the bounding box.
    vector3 center() const {
        vector3 result;
        result.x() = (m_max.x() + m_min.x()) / 2;
        result.y() = (m_max.y() + m_min.y()) / 2;
        result.t() = (m_max.t() + m_min.t()) / 2;
        return result;
    }

    vector3 widths() const {
        return {
            m_max.x() - m_min.x(),
            m_max.y() - m_min.y(),
            m_max.t() - m_min.t()
        };
    }

    /// Returns true if this bounding box fully contains `other`.
    bool contains(const bounding_box& other) const {
        return vector3::less_eq(m_min, other.m_min) && vector3::less_eq(other.m_max, m_max);
    }

    /// Returns true if this bounding box has a non-empty intersection
    /// with the other bounding box.
    bool intersects(const bounding_box& other) const {
        return      (min().x() <= other.max().x() && max().x() >= other.min().x())
                 && (min().y() <= other.max().y() && max().y() >= other.min().y())
                 && (min().t() <= other.max().t() && max().t() >= other.min().t());
    }

    /// Returns the intersection of `*this` and `other`.
    /// If the boxes do not intersect, an empty bounding box will be returned.
    bounding_box intersection(const bounding_box& other) const {
        if (!intersects(other)) {
            return {};
        }

        // Pick the larger start value and the smaller end value in every coordinate.
        return { vector3::max(min(), other.min()), vector3::min(max(), other.max()) };
    }

    /// Returns a minimum bounding box that contains
    /// both \p *this and \p other.
    bounding_box extend(const bounding_box& other) const {
        return { vector3::min(m_min, other.m_min), vector3::max(m_max, other.m_max) };
    }

    float size() const {
        return float(rect_base::size());
    }

private:
    vector3 m_min;
    vector3 m_max;
};

} // namespace geodb

#endif // GEODB_BOUNDING_BOX_HPP
