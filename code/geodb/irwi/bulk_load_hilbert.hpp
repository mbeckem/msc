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

namespace geodb {

template<typename Tree>
class hilbert_loader : private bulk_load_common<Tree, hilbert_loader<Tree>> {
    using common_t = typename hilbert_loader::bulk_load_common;

    using typename common_t::state_type;
    using typename common_t::storage_type;

    using typename common_t::node_summary;
    using typename common_t::list_summary;
    using typename common_t::label_summary;

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
    hilbert_loader(Tree& tree, tpie::file_stream<tree_entry>& entries)
        : common_t(tree)
        , m_input(entries)
    {}

    void load() {
        u64 count = 0;

        tpie::temp_file current;
        {
            tpie::serialization_writer writer;
            writer.open(current.path());

            fmt::print("Creating leaves\n");
            count = create_leaves(writer);
        }

        size_t height = 1;
        while (count != 1) {
            geodb_assert(count > 1, "Input must never be empty");

            tpie::temp_file output;
            {
                tpie::serialization_reader last_level;
                tpie::serialization_writer next_level;
                last_level.open(current.path());
                next_level.open(output.path());

                fmt::print("Creating internal nodes at height {}\n", height);
                count = create_internals(last_level, count, next_level);
            }
            current = output;

            ++height;
        }

        tpie::serialization_reader reader;
        reader.open(current);
        storage().set_height(height);
        storage().set_size(m_input.size());
        storage().set_root(common_t::read_root_ptr(reader));
    }

private:
    /// Compute the minimum bounding box that contains all entries in the input set.
    /// This is necessary to scale the points into the space mapped by the hilbert curve.
    bounding_box get_total() {
        geodb_assert(m_input.size() != 0, "Input must not be empty");

        m_input.seek(0);
        tree_entry current = m_input.read();
        bounding_box total = current.unit.get_bounding_box();
        while (m_input.can_read()) {
            current = m_input.read();
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

        curve_point operator()(const point& p) const {
            curve_point result{ x(p.x()), y(p.y()), t(p.t()) };
            return result;
        }
    };

    void map_entries( tpie::file_stream<hilbert_entry>& output) {
        point_mapper mapper(get_total());

        m_input.seek(0);
        output.truncate(0);
        while (m_input.can_read()) {
            tree_entry entry = m_input.read();
            point center = entry.unit.center();

            hilbert_entry result;
            result.inner = entry;
            result.hilbert_index = curve::hilbert_index(mapper(center));
            output.write(result);
        }
    }

    u64 create_leaves(tpie::serialization_writer& writer) {
        // Augment the entries with their hilbert index
        // and sort them in ascending order.
        tpie::file_stream<hilbert_entry> entries;
        {
            entries.open();
            map_entries(entries);
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
            common_t::write_summary(writer, leaf);

            remaining -= count;
            ++leaves;
        }

        return leaves;
    }

    u64 create_internals(tpie::serialization_reader& reader, u64 count,
                         tpie::serialization_writer& writer) {
        u64 internals = 0;
        u64 remaining = count;

        auto insert_index_entry = [](const auto& list, u32 id, const list_summary& summary) {
            posting_type entry(id, summary.count, summary.trajectories);
            list->append(entry);
        };

        while (remaining) {
            const u32 count = std::min<u64>(remaining, state_type::max_internal_entries());

            // Build the internal node by reading the child summaries from the last level.
            // The inverted index can be constructed simply by taking these summaries
            // and appending them to the appropriate lists.
            internal_ptr internal = storage().create_internal();
            index_ptr index = storage().index(internal);
            for (u32 i = 0; i < count; ++i) {
                auto node_cb = [&](const node_summary& ns) {
                    storage().set_child(internal, i, ns.ptr);
                    storage().set_mbb(internal, i, ns.mbb);
                    insert_index_entry(index->total(), i, ns.total);
                };

                auto label_cb = [&](const label_summary& ls) {
                    insert_index_entry(index->find_or_create(ls.label)->postings_list(), i, ls.summary);
                };

                common_t::read_summary(reader, node_cb, label_cb);
            }
            storage().set_count(internal, count);

            // Summarize this node for the next level.
            common_t::write_summary(writer, internal);

            remaining -= count;
            ++internals;
        }
        return internals;
    }

private:
    using common_t::state;
    using common_t::storage;

private:
    tpie::file_stream<tree_entry>& m_input;

    /// Threshold after which the heuristic becomes active at the leaf level.
    /// When this many items are already within the current leaf,
    /// new items will only be accepted under certain conditions.
    const u32 m_threshold = state_type::max_leaf_entries() / 2;

    /// When the item threshold is reached, don't accept
    /// any more items if they make the bounding box
    /// grow by more than this factor.
    const double m_max_growth = 1.2;
};

template<typename Tree>
void bulk_load_hilbert(Tree& tree, tpie::file_stream<tree_entry>& entries) {
    if (!tree.empty()) {
        throw std::invalid_argument("Tree must be empty"); // TODO
    }
    if (entries.size() == 0) {
        return;
    }

    hilbert_loader<Tree> loader(tree, entries);
    loader.load();
}

} // namespace geodb

#endif // GEODB_IRWI_BULK_LOAD_HILBERT_HPP
