#ifndef GEODB_KLEE_HPP
#define GEODB_KLEE_HPP

#include "geodb/common.hpp"
#include "geodb/rectangle.hpp"

#include <vector>

/// \file
/// Contains the implementation of Bentley's Algorithm,
/// which computes the area of the union of a set of rectangles.

namespace geodb {

/// Computes the area of the union of all given 2d rectangles.
///
/// Runtime: O(n*log n).
double union_area(const std::vector<rect2d>& rects);

/// Computes the volume of the union of all given 3d rectangles.
///
/// Runtime: O(n^2 * log n). Simply executes the 2d-dimensional
/// algorithm in a loop.
double union_area(const std::vector<rect3d>& rects);

} // namespace geodb

#endif // GEODB_KLEE_HPP
