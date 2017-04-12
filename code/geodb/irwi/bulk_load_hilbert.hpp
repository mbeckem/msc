#ifndef GEODB_IRWI_BULK_LOAD_HILBERT_HPP
#define GEODB_IRWI_BULK_LOAD_HILBERT_HPP

#include "geodb/common.hpp"
#include "geodb/hilbert.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/bulk_load_common.hpp"
#include "geodb/utility/external_sort.hpp"

#include <fmt/ostream.h>
#include <tpie/file_stream.h>
#include <tpie/serialization_stream.h>

/// \file
/// Bulk loading based on hilbert values.

namespace geodb {

/// The hilbert_loader loads a set of entries into a tree by
/// sorting them by their hilbert value first (see \ref hilbert_curve).
/// Entries are then visited in linear order to form leaf nodes,
/// which in turn are packed together into internal nodes until
/// only the root remains.
template<typename Tree>
class hilbert_loader : public bulk_load_common<Tree, hilbert_loader<Tree>> {
    using common_t = typename hilbert_loader::bulk_load_common;

    using typename common_t::state_type;
    using typename common_t::storage_type;

    using typename common_t::node_summary;
    using typename common_t::list_summary;
    using typename common_t::label_summary;

    using typename common_t::level_files;
    using typename common_t::last_level_streams;
    using typename common_t::next_level_streams;
    using typename common_t::subtree_result;

    using leaf_ptr = typename state_type::leaf_ptr;
    using internal_ptr = typename state_type::internal_ptr;
    using node_ptr = typename state_type::node_ptr;

    using index_ptr = typename state_type::index_ptr;

    using posting_type = typename state_type::posting_type;

    // Three dimensional curve where every point coordiante has 16 bits of precision,
    // for a total of 2^48 possible hilbert index values.
    using curve = hilbert_curve<3, 16>;

    using curve_coordiante = typename curve::coordinate_t;
    using curve_point = typename curve::point_t;

public:
    explicit hilbert_loader(Tree& tree)
        : common_t(tree)
    {}

private:
    friend common_t;

    subtree_result load_impl(tpie::file_stream<tree_entry>& entries) {
        geodb_assert(entries.size() > 0, "entry file must not be empty");

        u64 count = 0;

        level_files files;
        {
            next_level_streams output(files);
            count = create_leaves(entries, output);
        }

        size_t height = 1;
        while (count != 1) {
            geodb_assert(count > 1, "Input must never be empty");

            level_files next_files;
            {
                last_level_streams input(files);
                next_level_streams output(next_files);

                fmt::print("Creating internal nodes at height {}...\n", height + 1);
                count = create_internals(count, input, output);
            }
            files = next_files;
            ++height;
        }

        last_level_streams input(files);
        return subtree_result(common_t::read_root_ptr(input.summaries), height, entries.size());
    }

private:
    /// Compute the minimum bounding box that contains all entries in the input set.
    /// This is necessary to scale the points into the space mapped by the hilbert curve.
    bounding_box get_total(tpie::file_stream<tree_entry>& entries) {
        geodb_assert(entries.size() != 0, "Input must not be empty");

        entries.seek(0);
        tree_entry current = entries.read();
        bounding_box total = current.unit.get_bounding_box();
        while (entries.can_read()) {
            current = entries.read();
            total = total.extend(current.unit.get_bounding_box());
        }
        return total;
    }

    struct hilbert_entry {
        u64 hilbert_index;
        tree_entry inner;
    };

    /// Maps a value in [min, max] to a value in [0, 2^precision - 1]
    /// which can be used as a coordinate for the hilbert curve.
    struct coordiante_mapper {
        double min, max, d;

        coordiante_mapper(double min, double max)
            : min(min)
            , max(max)
            , d(min < max ? max - min : 1)
        {
            geodb_assert(min <= max, "invalid min/max values");
        }

        curve_coordiante operator()(double c) const {
            geodb_assert(min <= c && c <= max, "value not in range");

            static constexpr u64 coord_max = (1 << curve::precision) - 1;

            // scaled into [0, 1]:
            double s = (c - min) / d;
            // scaled into [0, 2^precision - 1].
            return curve_coordiante{static_cast<u64>(s * static_cast<double>(coord_max))};
        }
    };

    /// Maps a point contained the bounding box given by "total"
    /// into the space [0, 2^precision-1]^3 visited by the hilbert curve.
    struct point_mapper {
        coordiante_mapper x, y, t;

        point_mapper(const bounding_box& total)
            : x(total.min().x(), total.max().x())
            , y(total.min().y(), total.max().y())
            , t(total.min().t(), total.max().t())
        {}

        curve_point operator()(const vector3& p) const {
            curve_point result{ x(p.x()), y(p.y()), t(p.t()) };
            return result;
        }
    };

    void map_entries(tpie::file_stream<tree_entry>& input, tpie::file_stream<hilbert_entry>& output) {
        point_mapper mapper(get_total(input));

        input.seek(0);
        output.truncate(input.size());
        output.seek(0);
        while (input.can_read()) {
            tree_entry entry = input.read();
            vector3 center = entry.unit.center();

            hilbert_entry result;
            result.inner = entry;
            result.hilbert_index = curve::hilbert_index(mapper(center));
            output.write(result);
        }
    }

    u64 create_leaves(tpie::file_stream<tree_entry>& input, next_level_streams& output) {
        // Augment the entries with their hilbert index
        // and sort them in ascending order.
        tpie::file_stream<hilbert_entry> entries;
        {
            entries.open();
            map_entries(input, entries);
            external_sort(entries, [&](const hilbert_entry& a, const hilbert_entry& b) {
                return a.hilbert_index < b.hilbert_index;
            });
        }

        // Then, iterate the entries in linear order and group them as leaves.
        u64 leaves = 0;
        u64 remaining = entries.size();

        entries.seek(0);
        while (remaining) {
            u32 count = std::min<u64>(remaining, m_threshold);

            leaf_ptr leaf = storage().create_leaf();

            // Take at least count entries from the input.
            for (u32 i = 0; i < count; ++i) {
                tree_entry entry = entries.read().inner;
                storage().set_data(leaf, i, entry);
            }
            storage().set_count(leaf, count);

            // Take more entries until the max growth would be violated.
            bounding_box mbb = state().get_mbb(leaf);
            const double max_size = mbb.size() * m_max_growth;  // Maybe recompute after the addition of new elements?

            // As long as there are more entries and there is space left in the leaf:
            while (count < remaining && count < state_type::max_leaf_entries()) {
                tree_entry entry = entries.peek().inner;
                bounding_box new_mbb = mbb.extend(entry.unit.get_bounding_box());
                if (new_mbb.size() > max_size) {
                    break;
                }

                entries.skip();
                mbb = new_mbb;
                storage().set_data(leaf, count++, entry);
            }

            storage().set_count(leaf, count);
            common_t::write_summary(output, leaf);

            remaining -= count;
            ++leaves;
        }

        return leaves;
    }

    u64 create_internals(u64 count, last_level_streams& input, next_level_streams& output) {
        u64 internals = 0;
        u64 remaining = count;

        std::vector<node_summary> node_content;
        node_content.reserve(state_type::max_internal_entries());
        while (remaining) {
            const u32 count = std::min<u64>(remaining, state_type::max_internal_entries());

            node_content.clear();
            for (u32 i = 0; i < count; ++i) {
                node_content.push_back(input.summaries.read());
            }

            internal_ptr internal = common_t::build_internal_node(node_content, input.label_summaries);
            common_t::write_summary(output, internal);

            remaining -= count;
            ++internals;
        }
        return internals;
    }

private:
    using common_t::state;
    using common_t::storage;

private:
    /// Threshold after which the heuristic becomes active at the leaf level.
    /// When this many items are already within the current leaf,
    /// new items will only be accepted under certain conditions.
    const u32 m_threshold = state_type::max_leaf_entries() / 2;

    /// When the item threshold is reached, don't accept
    /// any more items if they make the bounding box
    /// grow by more than this factor.
    const double m_max_growth = 1.2;
};

} // namespace geodb

#endif // GEODB_IRWI_BULK_LOAD_HILBERT_HPP
