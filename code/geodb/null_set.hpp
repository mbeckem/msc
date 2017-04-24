#ifndef GEODB_NULL_SET_HPP
#define GEODB_NULL_SET_HPP

#include "geodb/common.hpp"

#include <initializer_list>

/// \file
/// Implements a "set" that does nothing.

namespace geodb {

/// The null set is a trivial probabilistic data structure
/// that uses zero space and "contains" every element.
/// Its interface is compatible with the \ref interval_set
/// and the \ref bloom_filter.
///
/// It is used by the quickload algorithm which does not
/// need to store trajectory ids.
template<typename T>
class null_set {
public:
    using value_type = T;

public:
    template<typename NullSets>
    static null_set set_union(const NullSets&) {
        return {};
    }

    template<typename NullSets>
    static null_set set_intersection(const NullSets&) {
        return {};
    }

public:
    null_set() = default;

    null_set(std::initializer_list<value_type>) {}

    template<typename Iter>
    null_set(Iter, Iter) {}

    void add(const value_type&) {}

    template<typename Iter>
    void assign(Iter, Iter) {}

    void clear() {}

    bool empty() const { return false; }

    bool contains(value_type&) const { return true; }

    null_set intersection_with(const null_set&) const { return {}; }

    null_set union_with(const null_set&) const { return {}; }
};

} // namespace geodb

#endif // GEODB_NULL_SET_HPP
