#ifndef GEODB_RECTANGLE_HPP
#define GEODB_RECTANGLE_HPP

#include "geodb/common.hpp"
#include "geodb/vector.hpp"

#include <ostream>

namespace geodb {

/// Vector should be some subclass of vector_base from vector.hpp.
template<typename Vector, typename Derived>
class rect_base {
public:
    using vector_type = Vector;

public:
    rect_base() = default;

    rect_base(const vector_type& min, const vector_type& max)
        : m_min(min)
        , m_max(max)
    {
        geodb_assert(vector_type::less_eq(min, max),
                     "Min point must be <= max point in all coordinates");
    }

    /// Returns the minimum point of this rectangle.
    const vector_type& min() const { return m_min; }

    /// Returns the maximum point of this rectangle.
    const vector_type& max() const { return m_max; }

    /// Returns the area (hypervolume in d dimensions) of this rectangle.
    double size() const {
        vector_type diff = max() - min();
        double size = 1;
        enumerate<diff.size()>([&](auto i) {
            size *= diff.get(i);
        });
        return size;
    }

    /// Returns true if this rectangle is empty,
    /// i.e. when coordinates are equal in at least one dimension.
    bool empty() const {
        vector_type diff = max() - min();
        bool equal = false;
        enumerate<diff.size()>([&](auto i) {
            equal = equal || diff.get(i) == 0;
        });
        return equal;
    }

private:
    friend bool operator==(const Derived& a, const Derived& b) {
        return a.min() == b.min() && a.max() == b.max();
    }

    friend bool operator!=(const Derived& a, const Derived& b) {
        return a.min() != b.min() || a.max() != b.max();
    }

    friend std::ostream& operator<<(std::ostream& o, const Derived& d) {
        return o << "{min: " << d.min() << ", max: " << d.max() << "}";
    }

private:
    vector_type m_min{}, m_max{};
};

/// A two-dimensional rectangle (with double coordiantes).
class rect2d : public rect_base<vector2d, rect2d> {
    using rect_base::rect_base;
};

/// A three-dimensional rectangle (with double coordinates).
class rect3d : public rect_base<vector3d, rect3d> {
    using rect_base::rect_base;
};

} // namespace geodb

#endif // GEODB_RECTANGLE_HPP
