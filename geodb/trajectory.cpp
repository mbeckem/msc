#include "geodb/trajectory.hpp"

#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/segment.hpp>
#include <boost/geometry/algorithms/intersects.hpp>

namespace geodb {

bool trajectory_unit::intersects(const bounding_box& b) const {
    namespace bg = boost::geometry;

    // Using double for all coordinates.
    using point_t = bg::model::point<double, 3, bg::cs::cartesian>;
    using line_t = bg::model::segment<point_t>;
    using box_t = bg::model::box<point_t>;

    // Convert point to boost geometry type.
    auto convert_point = [](const point& p) {
        return point_t(p.x(), p.y(), p.t());
    };

    box_t box(convert_point(b.min()), convert_point(b.max()));
    line_t line(convert_point(start), convert_point(end));
    return bg::intersects(box, line);
}

} // namespace geodb
