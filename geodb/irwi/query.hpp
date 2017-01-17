#ifndef IRWI_QUERY_HPP
#define IRWI_QUERY_HPP

#include "geodb/common.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/interval.hpp"

#include <unordered_set>
#include <vector>

namespace geodb {

struct simple_query {
    bounding_box rect;
    std::unordered_set<label_type> labels;     // TODO: "any"
};

struct sequenced_query {
    std::vector<simple_query> queries;
};

struct match {
    trajectory_id_type id;
    std::vector<u32> units;
};

} // namespace geodb

#endif // IRWI_QUERY_HPP
