#ifndef TRAJECTORY_HPP
#define TRAJECTORY_HPP

#include "geodb/bounding_box.hpp"
#include "geodb/common.hpp"
#include "geodb/point.hpp"

#include <ostream>

namespace geodb {

using label_type = u32;

using trajectory_id_type = u64;

/// A spatio-textual trajectory unit stores the spatial line segment
/// and a label identifier.
struct trajectory_unit {
    // Fields are unordered (i.e. xbegin not necessarily < xend)
    // because they describe the temporal/spatial line.
    point start;
    point end;
    label_type label;

    bounding_box get_bounding_box() const {
        return { point::min(start, end), point::max(start, end) };
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

struct annotated_trajectory_unit {
    trajectory_id_type tid; ///< id of the parent trajectory.
    u32 uid;                ///< index of this unit in its parent.
    trajectory_unit unit;   ///< the trajectory unit.
};

/// A spatio-textual trajectory is a list of
/// spatio-textual trajectory units, together with
/// a unique identifier.
struct trajectory {
    trajectory_id_type id;
    std::vector<trajectory_unit> units;
};

} // namespace geodb

#endif // TRAJECTORY_HPP
