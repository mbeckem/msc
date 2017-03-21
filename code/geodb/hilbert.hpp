#ifndef GEODB_HILBERT_HPP
#define GEODB_HILBERT_HPP

#include "geodb/common.hpp"
#include "geodb/vector.hpp"

#include <bitset>

namespace geodb {

/// This class template implements the hilbert curve, or,
/// more precisely, its nth approxmiation (n is the "precision" parameter).
///
/// The curve of Dimension `d` and Precision `p` visits all points
/// with `d` coordinates, where each coordinate has `p` bits.
/// In other words, every point in [0, ..., 2^p - 1]^d lies on the curve,
/// making for a total of 2^(p * n) points.
/// Every point can be mapped to its index on the hilbert curve (a bijective relation).
///
/// Coordinates are represented by a bitset (of p bits). Points are an array
/// of `d` coordinates.
template<u32 Dimension, u32 Precision>
class hilbert_curve {
public:
    /// Dimensionality of the hilbert curve.
    static constexpr u32 dimension = Dimension;

    /// The number of bits in each point coordinate.
    static constexpr u32 precision = Precision;

    /// There are 2 ^ dimension (hyper-) cubes at each level
    /// of the recursive curve definition.
    static constexpr u32 cubes = 1 << dimension;

    /// Identifies the vertex within a N dimensional hypercube.
    /// I.e. 00 -> lower left, 11 upper right etc.
    using bitset_t = std::bitset<dimension>;

    /// Represents a single coordinate.
    using coordinate_t = std::bitset<precision>;

    /// Represents a single point in n-dimensional space,
    /// using `precision` bits for each coordinate.
    using point_t = std::array<coordinate_t, dimension>;

    /// Represents a single index on the hilbert curve.
    using index_t = u64;

    /// The number of possible hilbert indicies with the chosen
    /// dimension and precision.
    /// All indices in [0, ..., index_count) represent valid points.
    static constexpr index_t index_count = 1 << (dimension * precision);

    static_assert(dimension * precision <= 64,
                  "Must be able to represent the index in 64 bits");

    /// Rotates the bitset to the left by `m` bits.
    static bitset_t rotate_left(bitset_t p, u32 m) {
        geodb_assert(m <= dimension, "rotating by too many bits");
        return (p << m) | (p >> (dimension - m));
    }

    /// Rotates the bitset to the right by `m` bits.
    static bitset_t rotate_right(bitset_t p, u32 m) {
        geodb_assert(m <= dimension, "rotating by too many bits");
        return (p >> m) | (p << (dimension - m));
    }

    /// Computes the gray code of the given index (where index must be < (2^Dimension)).
    static bitset_t gray_code(u32 index) {
        geodb_assert(index < cubes, "index out of bounds");
        return index ^ (index >> 1);
    }

    /// Reverses the gray code. Returns the original index.
    static u32 gray_code_inverse(bitset_t gray_code) {
        u32 mask;
        u32 num = gray_code.to_ulong();
        for (mask = num >> 1; mask != 0; mask >>= 1) {
            num = num ^ mask;
        }
        return num;
    }

    /// Returns the entry point in the hybercube with the given index.
    static bitset_t entry(u32 index) {
        return index > 0 ? gray_code(2 * ((index - 1) / 2)) : 0;
    }

    /// Returns the exit point in the hypercube with the given index.
    static bitset_t exit(u32 index) {
        static constexpr bitset_t op = bitset_t(1 << (dimension - 1));
        return entry(cubes - 1 - index) ^ op;
    }

    /// Returns the position of the bit that changes when going
    /// from subcube `index` to subcube `index + 1`.
    static u32 changed_dimension(u32 index) {
        geodb_assert(index < cubes, "index out of bounds");

        // Manually compute trailing set bits.
        u32 i = 0;
        bitset_t b = index;
        for (; i < dimension; ++i) {
            if (!b.test(i)) {
                break;
            }
        }
        return i;
    }

    /// Direction the arrow takes within the subcube of the given index.
    static u32 change(u32 index) {
        if (index == 0) {
            return 0;
        } else if (index % 2 == 0) {
            return changed_dimension(index - 1) % dimension;
        } else {
            return changed_dimension(index) % dimension;
        }
    }

    /// Transforms `pos`.
    static bitset_t transform(bitset_t e, u32 d, bitset_t b) {
        return rotate_right(b ^ e, d + 1);
    }


    /// Inverts the transformation made by `transform(e, d, b)`.
    static bitset_t transform_inverse(bitset_t e, u32 d, bitset_t b) {
        return transform(rotate_right(e, d + 1), dimension - d - 2, b);
    }

    /// Returns the hilbert index of the given point.
    static index_t hilbert_index(const point_t& point) {
        // Returns the bitset [bit(p_{n-1}, i), ..., bit(p_0, i)].
        auto bits = [&](u32 i) {
            bitset_t l;
            for (u32 p = 0; p < dimension; ++p) {
                l[p] = point[p][i];
            }
            return l;
        };

        index_t h = 0;
        bitset_t e = 0;
        u32 d = 0;
        for (u32 i = precision; i-- > 0; ) {
            bitset_t l = transform(e, d, bits(i));
            u32 w = gray_code_inverse(l);
            e = e ^ (rotate_left(entry(w), d + 1));
            d = (d + change(w) + 1) % dimension;
            h = (h << dimension) | w;
        }

        return h;
    }

    /// Inverses the hilbert_index mapping.
    static point_t hilbert_index_inverse(index_t h) {
        static constexpr u32 dimension_mask = (1 << dimension) - 1;

        /// Returns [bit(h, i * n + n - 1), ..., bit(h, i * n)]
        auto bits = [&](u32 i) {
            const u32 base = i * dimension;
            return static_cast<u32>(h >> base) & dimension_mask;
        };

        point_t point{};

        bitset_t e = 0;
        u32 d = 0;
        for (u32 i = precision; i-- > 0; ) {
            u32 w = bits(i);

            bitset_t l = transform_inverse(e, d, gray_code(w));

            for (u32 j = 0; j < dimension; ++j) {
                point[j][i] = l[j];
            }

            e = e ^ rotate_left(entry(w), d + 1);
            d = (d + change(w) + 1) % dimension;
        }
        return point;
    }
};

} // namespace geodb

#endif // GEODB_HILBERT_HPP
