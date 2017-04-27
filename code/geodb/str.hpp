#ifndef GEODB_STR_HPP
#define GEODB_STR_HPP

#include "geodb/common.hpp"
#include "geodb/utility/tuple_utils.hpp"
#include "geodb/utility/external_sort.hpp"

#include <tpie/file_stream.h>

#include <algorithm>
#include <cmath>
#include <tuple>

/// \file
/// Contains the implementation of the Sort-Tile-Recursive algorithm.

namespace geodb {

/// Implements the Sort Tile Recursive algorithm for
/// file streams and in-memory vectors.
///
/// Input sequences are sorted using multiple comparison objects.
/// The number of comparison objects is the number of dimensions.
/// The order of the comparsion objects defines the order in
/// which tiles are sorted.
///
/// For example, str_impl<Comp1, Comp2> will sort the entire sequence
/// by Comp1 first, tile it into subsequences and then sort those
/// using Comp2.
template<typename... Comparators>
class str_impl {
public:
    static const u32 dimensions = sizeof...(Comparators);

    static_assert(dimensions >= 1, "invalid number of dimensions");

public:
    /// \param leaf_size
    ///     The number of entries per leaf.
    /// \param comps
    ///     The comparison objects.
    str_impl(u32 leaf_size, Comparators&... comps)
        : m_leaf_size(leaf_size)
        , m_comps(comps...)
    {
        geodb_assert(m_leaf_size > 0, "invalid leaf size");
    }

    /// Runs the STR algorithm on the provided stream.
    /// This class currently supports tpie::file_stream<T> and std::vector<T>.
    template<typename Stream>
    void run(Stream&& stream) {
        return run_recursive<dimensions>(stream, 0, stream.size());
    }

private:
    template<u32 Dimension, typename Stream,
             std::enable_if_t<(Dimension >= 1)>* = nullptr>
    void run_recursive(Stream&& stream, const u64 offset, const u64 size) {
        // Sort the current (sub-) stream using the comparator at this dimension.
        sort(stream, offset, size, comparator<Dimension>());

        // Recursive invocation if there are more dimensions to tile.
        // Divide the input file into slabs and sort those.
        if (Dimension > 1) {
            // Number of leaves required for the input size.
            const u64 leaves = (size + m_leaf_size - 1) / m_leaf_size;

            // Number of leaves per run at the current dimension.
            const u64 slab_leaves = std::max(1.0, std::ceil(std::pow(leaves, double(Dimension - 1) / double(Dimension))));

            // Number of items per run at the current dimension.
            const u64 slab_size = m_leaf_size * slab_leaves;

            // Recursively visit the child slabs.
            u64 slab_start = offset;
            u64 remaining = size;
            while (remaining) {
                const u64 count = std::min(remaining, slab_size);

                // Recursive sort.
                run_recursive<Dimension - 1>(stream, slab_start, count);

                remaining -= count;
                slab_start += count;
            }
        }
    }

    // Base case for template recursion to make the compiler happy.
    template<u32 Dimension, typename Stream,
             std::enable_if_t<Dimension == 0>* = nullptr>
    void run_recursive(Stream&&, u64, u64) {}

    template<typename T, typename Comp>
    void sort(tpie::file_stream<T>& stream, u64 offset, u64 size, Comp&& comp) {
        external_sort(stream, offset, size, comp);
    }

    template<typename T, typename Comp>
    void sort(std::vector<T>& vec, u64 offset, u64 size, Comp&& comp) {
        auto first = vec.begin() + offset;
        auto last = first + size;
        std::sort(first, last, comp);
    }

private:
    // Returns the comparator for the given dimension.
    template<u32 Dimension>
    decltype(auto) comparator() {
        return std::get<dimensions - Dimension>(m_comps);
    }

private:
    const u32 m_leaf_size;
    const std::tuple<Comparators&...> m_comps;
};

/// Runs the STR algorithm on the provided stream.
/// \sa str_impl
template<typename T, typename... Comps>
void sort_tile_recursive(tpie::file_stream<T>& stream, u32 leaf_size, Comps&&... comps) {
    str_impl<Comps...> str(leaf_size, comps...);
    str.run(stream);
}

/// Runs the STR algorithm on the provided vector.
/// \sa str_impl
template<typename T, typename... Comps>
void sort_tile_recursive(std::vector<T>& vec, u32 leaf_size, Comps&&... comps) {
    str_impl<Comps...> str(leaf_size, comps...);
    str.run(vec);
}

} // namespace geodb

#endif // GEODB_STR_HPP
