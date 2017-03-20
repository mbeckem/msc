#ifndef LABEL_COUNT_HPP
#define LABEL_COUNT_HPP

#include "geodb/common.hpp"
#include "geodb/trajectory.hpp"

namespace geodb {

/// A label id and a count of trajectory units.
struct label_count {
    label_type label = 0;
    u64 count = 0;

    label_count() = default;

    label_count(label_type label, u64 count)
        : label(label)
        , count(count)
    {}
};

} // namespace geodb

#endif // LABEL_COUNT_HPP
