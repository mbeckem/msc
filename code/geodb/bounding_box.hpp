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
        result.x() = (max().x() + min().x()) / 2;
        result.y() = (max().y() + min().y()) / 2;
        result.t() = (max().t() + min().t()) / 2;
        return result;
    }

    vector3 widths() const {
        return {
            max().x() - min().x(),
            max().y() - min().y(),
            max().t() - min().t()
        };
    }

    /// Returns true if this bounding box fully contains `other`.
    bool contains(const bounding_box& other) const {
        return vector3::less_eq(min(), other.min()) && vector3::less_eq(other.max(), max());
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
        return { vector3::min(min(), other.min()), vector3::max(max(), other.max()) };
    }

    float size() const {
        return float(rect_base::size());
    }
};

} // namespace geodb

#endif // GEODB_BOUNDING_BOX_HPP
