#ifndef GEODB_IRWI_BULK_LOAD_STR
#define GEODB_IRWI_BULK_LOAD_STR

#include "geodb/common.hpp"
#include "geodb/str.hpp"
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

/// \file
/// Bulk loading based on the STR algorithm.

namespace geodb {

/// This class implements the STR bulk loading algorithm.
/// Leaf records are tiled by sorting them by different criteria
/// (label, x, y, t). They are then grouped into leaf nodes,
/// which are in turn grouped into internal nodes, until only one
/// node remains.
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

    using typename common_t::level_files;
    using typename common_t::last_level_streams;
    using typename common_t::next_level_streams;

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
    enum class sort_mode {
        label_first,
        label_last,
    };

public:
    explicit str_loader(Tree& tree, sort_mode mode = sort_mode::label_first)
        : common_t(tree)
        , m_mode(mode)
        , m_min_size(std::min(tree.max_leaf_entries(), tree.max_internal_entries())) // FIXME is this the right thing to do?
        , m_leaf_size(m_min_size)
        , m_internal_size(m_min_size)
    {}

private:
    friend common_t;

    subtree_result load_impl(tpie::file_stream<tree_entry>& input) {
        fmt::print("Sorting entries.\n");
        sort(input);

        level_files files;
        u64 count = 0;
        {
            fmt::print("Creating leaves.\n");

            next_level_streams output(files);
            count = create_leaves(input, output);
        }

        size_t height = 1;
        while (count != 1) {
            geodb_assert(count > 0, "Input must never be empty");

            level_files next_files;
            {
                fmt::print("Creating internal nodes at height {}.\n", height + 1);

                last_level_streams last_level(files);
                next_level_streams next_level(next_files);
                count = create_internals(count, last_level, next_level);
            }
            files = next_files;
            ++height;
        }

        last_level_streams last_level(files);
        return subtree_result(common_t::read_root_ptr(last_level.summaries), height, input.size());
    }

    // Create leaf-sized chunks by sorting the input
    // using the different dimension: label, x, y, t.
    void sort(tpie::file_stream<tree_entry>& input) {
        // The order of the comparison object defines the tiling order.
        switch (m_mode) {
        case sort_mode::label_first:
            sort_tile_recursive(input, m_leaf_size, cmp_label(), cmp_x(), cmp_y(), cmp_t());
            break;
        case sort_mode::label_last:
            sort_tile_recursive(input, m_leaf_size, cmp_x(), cmp_y(), cmp_t(), cmp_label());
            break;
        }
    }

    /// Takes chunks of size `m_leaf_size` items and packs them into leaf nodes.
    /// References to these nodes (and their summary) are written to `refs`.
    /// Returns the number of leaves.
    u64 create_leaves(tpie::file_stream<tree_entry>& input, next_level_streams& output) {
        u64 leaves = 0;
        u64 items_remaining = input.size();

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
            common_t::write_summary(output, leaf);

            items_remaining -= count;
            ++leaves;
        }

        return leaves;
    }

    /// Takes chunks of `m_internal_size` nodes and packs them together into internal nodes.
    /// Returns the number of internal nodes.
    u64 create_internals(u64 count, last_level_streams& input, next_level_streams& output)
    {
        u64 internals = 0;
        u64 items_remaining = count;

        std::vector<node_summary> node_content;
        node_content.reserve(m_internal_size);
        while (items_remaining) {
            const u32 count = std::min(items_remaining, m_internal_size);

            node_content.clear();
            for (u32 i = 0; i < count; ++i) {
                node_content.push_back(input.summaries.read());
            }

            internal_ptr internal = common_t::build_internal_node(node_content, input.label_summaries);
            common_t::write_summary(output, internal);

            items_remaining -= count;
            ++internals;
        }

        return internals;
    }

    using common_t::storage;
    using common_t::state;

private:
    const sort_mode m_mode;

    /// min(leaf_fanout, internal_fanout).
    const size_t m_min_size;

    /// Number of entries we are going to fill in every leaf node.
    const size_t m_leaf_size;

    /// Number of entries we are going to fill in every internal node.
    const size_t m_internal_size;
};

} // namespace geodb

#endif // GEODB_IRWI_BULK_LOAD_STR
