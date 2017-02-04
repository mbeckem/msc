#ifndef IRWI_STR_LOADER_HPP
#define IRWI_STR_LOADER_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"
#include "geodb/utility/external_sort.hpp"
#include "geodb/utility/file_allocator.hpp"

#include <fmt/format.h>
#include <tpie/fractional_progress.h>
#include <tpie/progress_indicator_base.h>
#include <tpie/progress_indicator_subindicator.h>
#include <tpie/serialization_stream.h>
#include <tpie/uncompressed_stream.h>

namespace geodb {

/// FIXME: Consistently use size_t or tpie::stream_size_type
template<typename Tree>
class str_loader {
    using storage_type = typename Tree::storage_type;
    using posting_type = typename Tree::posting_type;

    using node_ptr = typename Tree::node_ptr;
    using internal_ptr = typename Tree::internal_ptr;
    using leaf_ptr = typename Tree::leaf_ptr;

    using index_handle = typename Tree::index_handle;
    using list_handle = typename Tree::list_handle;

    using list_summary = typename Tree::list_type::summary_type;

    static point center(const tree_entry& e) {
        auto x = (e.unit.start.x() + e.unit.end.x()) / 2;
        auto y = (e.unit.start.y() + e.unit.end.y()) / 2;
        auto t = (e.unit.start.t() + e.unit.end.t()) / 2;
        return point(x, y, t);
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

    /// Represents a node of the lower level.
    /// Lower-level nodes are combined into internal nodes until
    /// only one node remains. This node becomes the root.
    struct node_reference {
        node_ptr ptr{};
        bounding_box mbb;
        list_summary total;     ///< Summary of `index->total`
        u64 num_labels = 0;     ///< This many summaries follow.

        template<typename Dst>
        friend void serialize(Dst& dst, const node_reference& n) {
            using tpie::serialize;
            serialize(dst, n.ptr);
            serialize(dst, n.mbb);
            serialize(dst, n.total);
            serialize(dst, n.num_labels);
        }

        template<typename Dst>
        friend void unserialize(Dst& dst, node_reference& n) {
            using tpie::unserialize;
            unserialize(dst, n.ptr);
            unserialize(dst, n.mbb);
            unserialize(dst, n.total);
            unserialize(dst, n.num_labels);
        }
    };

    struct label_summary {
        label_type label{};
        list_summary summary;

        template<typename Dst>
        friend void serialize(Dst& dst, const label_summary& s) {
            using tpie::serialize;
            serialize(dst, s.label);
            serialize(dst, s.summary);
        }

        template<typename Src>
        friend void unserialize(Src& src, label_summary& s) {
            using tpie::unserialize;
            unserialize(src, s.label);
            unserialize(src, s.summary);
        }
    };

public:
    str_loader(Tree& tree, tpie::uncompressed_stream<tree_entry>& input, tpie::progress_indicator_base& progress)
        : m_tree(tree)
        , m_input(input)
        , m_progress(progress)
        , m_items(input.size())
        , m_min_size(std::min(tree.max_leaf_entries(), tree.max_internal_entries())) // FIXME is this the right thing to do?
        , m_leaf_size(m_min_size)
        , m_internal_size(m_min_size)
    {
        geodb_assert(input.size() != 0, "Must not be empty");
    }


    // File format is a list of the following:
    // An instance of node_reference followed by a number of label_summary instances.
    // There are exactly node_reference.num_labels instances of label summaries per node.
    void build() {
        tpie::fractional_progress fp(&m_progress);
        tpie::fractional_subindicator sort_progress(fp, "sort", TPIE_FSI, 4000, "Sort leaf entries");
        tpie::fractional_subindicator leaves_progress(fp, "leaves", TPIE_FSI, 2000, "Build leaf nodes");
        tpie::fractional_subindicator internal_progress(fp, "internal", TPIE_FSI, 1000, "Build internal nodes");

        fp.init();
        sort(sort_progress);

        tpie::temp_file tmp;
        size_t count = 0;
        {
            tpie::serialization_writer next_level;
            next_level.open(tmp);
            count = create_leaves(next_level, leaves_progress);
            next_level.close();
        }

        tpie::serialization_reader reader;
        reader.open(tmp);

        build_tree(reader, count, 1, internal_progress);

        fp.done();
    }

    /// Recursive function that builds the tree.
    /// Takes a sequence of input nodes and combines them into internal nodes.
    /// Stops when there is only 1 node remaining.
    ///
    /// Note: Recursion isn't strictly neccessary (a loop would do) but
    /// it allows for nice recursive nesting of the fractional progress objects.
    void build_tree(tpie::serialization_reader& this_level, size_t count, size_t height, tpie::progress_indicator_base& progress) {
        geodb_assert(count > 0, "Input must never be empty");
        if (count == 1) {
            // This only remaining node becomes the root.
            node_reference node;
            this_level.unserialize(node);

            storage_type& storage = m_tree.storage();
            storage.set_height(height);
            storage.set_size(m_items);
            storage.set_root(node.ptr);
            return;
        }

        const std::string title = fmt::format("Level {}", height);
        tpie::fractional_progress fp(&progress);
        tpie::fractional_subindicator create(fp, "create", TPIE_FSI, 50, title.c_str());
        tpie::fractional_subindicator recurse(fp, "recurse", TPIE_FSI, 50, nullptr);
        fp.init();

        // More than one node on the current level. Create new leaves.
        tpie::temp_file tmp;
        size_t new_count = 0;
        {
            tpie::serialization_writer next_level;
            next_level.open(tmp);
            new_count = create_internals(this_level, count, next_level, create);
            next_level.close();
        }

        // Recursive step.
        tpie::serialization_reader reader;
        reader.open(tmp);
        build_tree(reader, new_count, height + 1, recurse);
        fp.done();
    }

    // Create leaf-sized chunks by sorting the input
    // using the different dimension: label, x, y, t.
    void sort(tpie::progress_indicator_base& progress) {
        sort_recursive(0, m_items, progress,
                       // The ordering here defines the order of the dimension in STR.
                       cmp_label(), cmp_x(), cmp_y(), cmp_t());
    }

    // Base case for template recursion to make the compiler happy.
    void sort_recursive(tpie::stream_size_type offset, tpie::stream_size_type size,
                        tpie::progress_indicator_base& progress) {
        unused(offset, size, progress);
    }

    /// Recursively sort the input file using the given comparators.
    template<typename Comp, typename... Comps>
    void sort_recursive(tpie::stream_size_type offset, tpie::stream_size_type size,
                        tpie::progress_indicator_base& progress,
                        Comp&& comp, Comps&&... comps) {
        constexpr size_t dimension = sizeof...(Comps) + 1;

        const std::string sort_title = fmt::format("Sort ({})", dimension);

        tpie::fractional_progress fp(&progress);
        tpie::fractional_subindicator sort(fp, "sort", TPIE_FSI, 1, sort_title.c_str());
        tpie::fractional_subindicator recurse(fp, "recurse", TPIE_FSI, 1, nullptr);
        fp.init();

        // Sort the input file using the current comp.
        external_sort(m_input, offset, size, comp, sort);

        if (dimension > 1) {
            // Divide the input file into slabs and sort those.
            const tpie::stream_size_type P = (size + m_leaf_size - 1) / m_leaf_size;
            const tpie::stream_size_type S = std::max(1.0, std::pow(P, double(dimension - 1) / double(dimension)));
            const tpie::stream_size_type slab_size = m_leaf_size * S;

            // Number of processed / remaining items.
            tpie::stream_size_type offset = 0;
            tpie::stream_size_type remaining = size;

            // Keep track of current index (for reporting only).
            const tpie::stream_size_type total_slabs = (size + slab_size - 1) / slab_size;
            tpie::stream_size_type current_slab = 0;

            // Recursivly visit the child slabs.
            recurse.init(remaining);
            while (remaining) {
                const size_t count = std::min(remaining, slab_size);
                const std::string title = fmt::format("Slab {} of {}", current_slab + 1, total_slabs);

                // Recursive sort.
                tpie::progress_indicator_subindicator sub(&recurse, count, title.c_str());
                sort_recursive(offset, count, sub, comps...);

                remaining -= count;
                offset += count;
                current_slab += 1;
            }
            recurse.done();
        }
        fp.done();
    }


    /// Takes chunks of size `m_leaf_size` items and packs them into leaf nodes.
    /// References to these nodes (and their summary) are written to `refs`.
    /// Returns the number of leaves.
    tpie::stream_size_type create_leaves(tpie::serialization_writer& refs, tpie::progress_indicator_base& progress) {
        tpie::stream_size_type leaves = 0;
        tpie::stream_size_type items_remaining = m_items;

        // Keep summaries for single leaves in memory.
        // The memory used for this is in O(leaf_size) and thus very small.
        std::vector<trajectory_id_type> all_ids;
        std::map<label_type, std::vector<trajectory_id_type>> label_ids;
        node_reference node;
        label_summary summary;

        m_input.seek(0);
        progress.init(items_remaining);
        while (items_remaining) {
            const u32 count = std::min(items_remaining, m_leaf_size);

            // Fill the new leaf and remember the trajectory ids & labels.
            storage_type& storage = m_tree.storage();
            leaf_ptr leaf = storage.create_leaf();
            for (u32 i = 0; i < count; ++i) {
                tree_entry entry = m_input.read();

                storage.set_data(leaf, i, entry);
                all_ids.push_back(entry.trajectory_id);
                label_ids[entry.unit.label].push_back(entry.trajectory_id);
            }
            storage.set_count(leaf, count);

            // Create a new node entry containing index information and mbb
            // and write it to the file.
            node.ptr = leaf;
            node.mbb = m_tree.get_mbb(leaf);
            make_simple_summary(all_ids, count, node.total);
            node.num_labels = label_ids.size();
            refs.serialize(node);

            // Write a summary for each label.
            for (auto& pair : label_ids) {
                summary.label = pair.first;
                make_simple_summary(pair.second, pair.second.size(), summary.summary);
                refs.serialize(summary);
            }

            items_remaining -= count;
            ++leaves;

            all_ids.clear();
            label_ids.clear();
            progress.step(count);
        }
        progress.done();

        return leaves;
    }

    /// Takes chunks of `m_internal_size` nodes and packs them together into internal nodes.
    /// Returns the number of internal nodes.
    tpie::stream_size_type create_internals(tpie::serialization_reader& this_level, tpie::stream_size_type count,
                                            tpie::serialization_writer& next_level,
                                            tpie::progress_indicator_base& progress)
    {
        tpie::stream_size_type internals = 0;
        tpie::stream_size_type items_remaining = count;

        node_reference node_ref;
        label_summary label_sum;

        auto insert_index_entry = [](const auto& list, u32 id, const list_summary& summary) {
            posting_type entry(id, summary.count, summary.trajectories);
            list->append(entry);
        };

        progress.init(items_remaining);
        while (items_remaining) {
            const u32 count = std::min(items_remaining, m_internal_size);

            // Assemble the new internal node by inserting the node data
            // into the node and its index. The index summaries for each child
            // were already created in the last phase.
            storage_type& storage = m_tree.storage();
            internal_ptr internal = storage.create_internal();
            index_handle index = storage.index(internal);
            for (u32 i = 0; i < count; ++i) {
                this_level.unserialize(node_ref);

                storage.set_child(internal, i, node_ref.ptr);
                storage.set_mbb(internal, i, node_ref.mbb);

                insert_index_entry(index->total(), i, node_ref.total);
                for (size_t l = 0; l < node_ref.num_labels; ++l) {
                    this_level.unserialize(label_sum);
                    list_handle list = index->find_or_create(label_sum.label)->postings_list();
                    insert_index_entry(list, i, label_sum.summary);
                }
            }
            storage.set_count(internal, count);

            // The node is complete, now summarize it for the next level.
            node_ref.ptr = internal;
            node_ref.mbb = m_tree.get_mbb(internal);
            node_ref.total = index->total()->summarize();
            node_ref.num_labels = index->size();
            next_level.serialize(node_ref);

            // Summarize every label.
            for (auto entry : *index) {
                label_sum.label = entry.label();
                label_sum.summary = entry.postings_list()->summarize();
                next_level.serialize(label_sum);
            }

            items_remaining -= count;
            ++internals;

            progress.step(count);
        }
        progress.done();

        return internals;
    }

    void make_simple_summary(std::vector<trajectory_id_type>& ids, u64 units, list_summary& out) {
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

        out.count = units;
        out.trajectories.assign(ids.begin(), ids.end());
    }

private:
    Tree& m_tree;
    tpie::uncompressed_stream<tree_entry>& m_input;
    tpie::progress_indicator_base& m_progress;

    /// Number of leaf items we are loading.
    const tpie::stream_size_type m_items;

    const size_t m_min_size;

    /// Number of entries we are going to fill in every leaf node.
    const size_t m_leaf_size;

    /// Number of entries we are going to fill in every internal node.
    const size_t m_internal_size;
};

/// Bulk loads an IRWI-Tree from a list of trajectory units using
/// the STR (Sort Tile Recursive) Algorithm.
/// Currently the tree has to be empty.
template<typename Tree>
void bulk_load_str(Tree& tree, tpie::uncompressed_stream<tree_entry>& entries, tpie::progress_indicator_base& progress) {
    if (!tree.empty()) {
        throw std::invalid_argument("Tree must be empty");
    }

    if (entries.size() == 0) {
        return;
    }

    str_loader<Tree> loader(tree, entries, progress);
    loader.build();
}

} // namespace geodb

#endif // IRWI_STR_LOADER_HPP
