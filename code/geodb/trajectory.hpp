#ifndef GEODB_TRAJECTORY_HPP
#define GEODB_TRAJECTORY_HPP

#include "geodb/bounding_box.hpp"
#include "geodb/common.hpp"
#include "geodb/vector.hpp"

#include <tpie/serialization2.h>

#include <ostream>
#include <string>
#include <vector>

/// \file
/// Classes representing trajectories.

namespace geodb {

using label_type = u32;

using trajectory_id_type = u32;

/// A spatio-textual trajectory unit stores the spatial line segment
/// and a label identifier.
struct trajectory_unit {
    // Fields are unordered (i.e. xbegin not necessarily < xend)
    // because they describe the temporal/spatial line.
    vector3 start{};
    vector3 end{};
    label_type label = 0;

    trajectory_unit() = default;

    trajectory_unit(const vector3& start, const vector3& end, label_type label)
        : start(start)
        , end(end)
        , label(label)
    {}

    bool intersects(const bounding_box& b) const;

    bounding_box get_bounding_box() const {
        return { vector3::min(start, end), vector3::max(start, end) };
    }

    vector3 center() const {
        auto x = (start.x() + end.x()) / 2;
        auto y = (start.y() + end.y()) / 2;
        auto t = (start.t() + end.t()) / 2;
        return vector3(x, y, t);
    }
};

inline bool operator==(const trajectory_unit& a, const trajectory_unit& b) {
    return a.start == b.start && a.end == b.end && a.label == b.label;
}

inline bool operator!=(const trajectory_unit& a, const trajectory_unit& b) {
    return !(a == b);
}

inline std::ostream& operator<<(std::ostream& o, const trajectory_unit& u) {
    return o << "{start: " << u.start << ", end: " << u.end << ", label: " << u.label << "}";
}

/// A spatio-textual trajectory is a list of
/// spatio-textual trajectory units, together with
/// a unique identifier.
struct trajectory {
    trajectory_id_type id;
    std::vector<trajectory_unit> units;
};

struct trajectory_element {
public:
    vector3 spatial;
    label_type textual = 0;

    trajectory_element() {}

    trajectory_element(vector3 spatial, label_type textual)
        : spatial(spatial), textual(textual) {}

private:
    template<typename Dst>
    friend void serialize(Dst& dst, const trajectory_element& e) {
        using tpie::serialize;
        serialize(dst, e.spatial);
        serialize(dst, e.textual);
    }

    template<typename Dst>
    friend void unserialize(Dst& dst, trajectory_element& e) {
        using tpie::unserialize;
        unserialize(dst, e.spatial);
        unserialize(dst, e.textual);
    }
};

struct point_trajectory {
public:
    trajectory_id_type id = 0;
    std::string description;
    std::vector<trajectory_element> entries;

    point_trajectory() {}

    point_trajectory(trajectory_id_type id, std::string description,
                     std::vector<trajectory_element> entries)
        : id(id), description(std::move(description)), entries(std::move(entries))
    {}

private:
    template<typename Dst>
    friend void serialize(Dst& dst, const point_trajectory& p) {
        using tpie::serialize;
        serialize(dst, p.id);
        serialize(dst, p.description);
        serialize(dst, p.entries);
    }

    template<typename Src>
    friend void unserialize(Src& src, point_trajectory& p) {
        using tpie::unserialize;
        unserialize(src, p.id);
        unserialize(src, p.description);
        unserialize(src, p.entries);
    }
};

} // namespace geodb

#endif // GEODB_TRAJECTORY_HPP
