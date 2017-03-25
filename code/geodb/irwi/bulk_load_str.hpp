#ifndef GEODB_IRWI_BULK_LOAD_STR
#define GEODB_IRWI_BULK_LOAD_STR

#include "geodb/common.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/bulk_load_common.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"
#include "geodb/utility/external_sort.hpp"
#include "geodb/utility/file_allocator.hpp"
#include "geodb/utility/noop.hpp"

#include <fmt/format.h>
#include <tpie/fractional_progress.h>
#include <tpie/progress_indicator_base.h>
#include <tpie/progress_indicator_subindicator.h>
#include <tpie/serialization_stream.h>
#include <tpie/uncompressed_stream.h>

namespace geodb {

/// This class implements the STR bulk loading algorithm.
/// Leaf records are tiled by sorting them by different criteria
/// (label, x, y, t). They are then grouped into leaf nodes,
/// which are in turn grouped into internal nodes, until only one
/// node remains.
///
/// FIXME: Consistently use size_t or tpie::stream_size_type
template<typename Tree>
class str_loader : public bulk_load_common<Tree, str_loader<Tree>> {
    using common_t = typename str_loader::bulk_load_common;

    using typename common_t::state_type;
    using storage_type = typename state_type::storage_type;
    using posting_type = typename state_type::posting_type;

    using node_ptr = typename state_type::node_ptr;
    using internal_ptr = typename state_type::internal_ptr;
    using leaf_ptr = typename state_type::leaf_ptr;

    using index_ptr = typename Tree::index_ptr;
    using list_ptr = typename state_type::list_ptr;

    using typename common_t::node_summary;
    using typename common_t::list_summary;
    using typename common_t::label_summary;
    using typename common_t::subtree_result;

    static vector3 center(const tree_entry& e) {
        return e.unit.center();
    }

    struct cmp_label {
        bool operator()(const tree_entry& e1, const tree_entry& e2) const {
            return e1.unit.label < e2.unit.label;
        }
    };

    struct cmp_x {
        bool operator()(const tree_entry& e1, const tree_entry& e2) const {
            return center(e1).x() < center(e2).x();
        }
    };

    struct cmp_y {
        bool operator()(const tree_entry& e1, const tree_entry& e2) const {
            return center(e1).y() < center(e2).y();
        }
    };

    struct cmp_t {
        bool operator()(const tree_entry& e1, const tree_entry& e2) const {
            return center(e1).t() < center(e2).t();
        }
    };

public:
    explicit str_loader(Tree& tree)
        : common_t(tree)
        , m_min_size(std::min(tree.max_leaf_entries(), tree.max_internal_entries())) // FIXME is this the right thing to do?
        , m_leaf_size(m_min_size)
        , m_internal_size(m_min_size)
    {}

private:
    friend common_t;

    subtree_result load_impl(tpie::file_stream<tree_entry>& input) {
        sort(input);

        tpie::temp_file tmp;
        u64 count = 0;
        {
            tpie::serialization_writer next_level;
            next_level.open(tmp);
            count = create_leaves(input, next_level);
        }

        size_t height = 1;
        while (count != 1) {
            geodb_assert(count > 0, "Input must never be empty");

            tpie::temp_file tmp_next;
            {
                tpie::serialization_reader reader;
                tpie::serialization_writer writer;
                reader.open(tmp.path());
                writer.open(tmp_next.path());
                count = create_internals(reader, count, writer);
            }
            tmp = tmp_next;

            ++height;
        }

        tpie::serialization_reader reader;
        reader.open(tmp.path());

        return subtree_result(common_t::read_root_ptr(reader), height, input.size());
    }

    // Create leaf-sized chunks by sorting the input
    // using the different dimension: label, x, y, t.
    void sort(tpie::file_stream<tree_entry>& input) {
        sort_recursive(input, 0, input.size(),
                       // The ordering here defines the order of recursive sort calls.
                       cmp_label(), cmp_x(), cmp_y(), cmp_t());
    }

    // Base case for template recursion to make the compiler happy.
    void sort_recursive(tpie::file_stream<tree_entry>& input, tpie::stream_size_type offset, tpie::stream_size_type size) {
        unused(input, offset, size);
    }

    /// Recursively sort the input file using the given comparators.
    template<typename Comp, typename... Comps>
    void sort_recursive(tpie::file_stream<tree_entry>& input,
                        const tpie::stream_size_type offset,
                        const tpie::stream_size_type size,
                        Comp&& comp, Comps&&... comps) {
        constexpr size_t dimension = sizeof...(Comps) + 1;

        // Sort the input file using the current comp.
        external_sort(input, offset, size, comp);

        if (dimension > 1) {
            // Divide the input file into slabs and sort those.
            const tpie::stream_size_type P = (size + m_leaf_size - 1) / m_leaf_size;
            const tpie::stream_size_type S = std::max(1.0, std::pow(P, double(dimension - 1) / double(dimension)));
            const tpie::stream_size_type slab_size = m_leaf_size * S;

            // Number of processed / remaining items.
            tpie::stream_size_type slab_start = offset;
            tpie::stream_size_type remaining = size;

            // Recursivly visit the child slabs.
            while (remaining) {
                const size_t count = std::min(remaining, slab_size);

                // Recursive sort.
                sort_recursive(input, slab_start, count, comps...);

                remaining -= count;
                slab_start += count;
            }
        }
    }


    /// Takes chunks of size `m_leaf_size` items and packs them into leaf nodes.
    /// References to these nodes (and their summary) are written to `refs`.
    /// Returns the number of leaves.
    tpie::stream_size_type create_leaves(tpie::file_stream<tree_entry>& input, tpie::serialization_writer& refs) {
        tpie::stream_size_type leaves = 0;
        tpie::stream_size_type items_remaining = input.size();

        input.seek(0);
        while (items_remaining) {
            const u32 count = std::min(items_remaining, m_leaf_size);

            // Fill the new leaf and remember the trajectory ids & labels.
            leaf_ptr leaf = storage().create_leaf();
            for (u32 i = 0; i < count; ++i) {
                tree_entry entry = input.read();
                storage().set_data(leaf, i, entry);
            }
            storage().set_count(leaf, count);
            common_t::write_summary(refs, leaf);

            items_remaining -= count;
            ++leaves;
        }

        return leaves;
    }

    /// Takes chunks of `m_internal_size` nodes and packs them together into internal nodes.
    /// Returns the number of internal nodes.
    tpie::stream_size_type create_internals(tpie::serialization_reader& this_level, tpie::stream_size_type count,
                                            tpie::serialization_writer& next_level)
    {
        tpie::stream_size_type internals = 0;
        tpie::stream_size_type items_remaining = count;

        auto insert_index_entry = [](const auto& list, u32 id, const list_summary& summary) {
            posting_type entry(id, summary.count, summary.trajectories);
            list->append(entry);
        };

        while (items_remaining) {
            const u32 count = std::min(items_remaining, m_internal_size);

            // Assemble the new internal node by inserting the node data
            // into the node and its index. The index summaries for each child
            // were already created in the last phase.
            internal_ptr internal = storage().create_internal();
            index_ptr index = storage().index(internal);
            for (u32 i = 0; i < count; ++i) {
                auto node_cb = [&](const node_summary& node) {
                    storage().set_child(internal, i, node.ptr);
                    storage().set_mbb(internal, i, node.mbb);
                    insert_index_entry(index->total(), i, node.total);
                };

                auto label_cb = [&](const label_summary& ls) {
                    insert_index_entry(index->find_or_create(ls.label)->postings_list(), i, ls.summary);
                };

                common_t::read_summary(this_level, node_cb, label_cb);
            }
            storage().set_count(internal, count);

            common_t::write_summary(next_level, internal);

            items_remaining -= count;
            ++internals;
        }

        return internals;
    }

    using common_t::storage;
    using common_t::state;

private:
    /// min(leaf_fanout, internal_fanout).
    const size_t m_min_size;

    /// Number of entries we are going to fill in every leaf node.
    const size_t m_leaf_size;

    /// Number of entries we are going to fill in every internal node.
    const size_t m_internal_size;
};

} // namespace geodb

#endif // GEODB_IRWI_BULK_LOAD_STR
