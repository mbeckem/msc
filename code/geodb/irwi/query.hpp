#ifndef GEODB_IRWI_QUERY_HPP
#define GEODB_IRWI_QUERY_HPP

#include "geodb/common.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/interval.hpp"

#include <unordered_set>
#include <vector>

/// \file
/// Contains definitions related to IRWI Tree queries.

namespace geodb {

struct simple_query {
    bounding_box rect;                      ///< Query rectangle.
    std::unordered_set<label_type> labels;  ///< Empty means "any".
};

struct sequenced_query {
    /// A list of simple query that have to be satisfied by every
    /// trajectory in the result set (i.e. the sequenced query is the logical AND
    /// of all simple queries).
    std::vector<simple_query> queries;
};

/// Represents a single matching trajectory unit.
struct unit_match {
    /// The index of the unit within its trajectory.
    u32 index = 0;

    /// The trajectory unit itself.
    trajectory_unit unit;

    unit_match() = default;

    unit_match(u32 index, trajectory_unit unit)
        : index(index)
        , unit(unit)
    {}
};

/// Represents a trajectory that satisfies a given sequenced query.
/// It contains the concrete set of trajectory units
/// that matched the query.
struct trajectory_match {
    /// The id of the matching trajectory.
    trajectory_id_type id = 0;

    /// The list of matching trajectory units and their indices in this trajectory.
    std::vector<unit_match> units;

    trajectory_match() = default;

    trajectory_match(trajectory_id_type id, std::vector<unit_match> units)
        : id(id)
        , units(std::move(units))
    {}
};

} // namespace geodb

#endif // GEODB_IRWI_QUERY_HPP
