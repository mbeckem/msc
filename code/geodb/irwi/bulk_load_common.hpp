#ifndef GEODB_IRWI_BULK_LOAD_COMMON_HPP
#define GEODB_IRWI_BULK_LOAD_COMMON_HPP

#include "geodb/common.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/type_traits.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/utility/noop.hpp"

#include <tpie/serialization2.h>
#include <tpie/serialization_stream.h>

namespace geodb {

template<typename Tree, typename Derived>
class bulk_load_common {
protected:
    using tree_type = Tree;
    using state_type = typename tree_type::state_type;
    using storage_type = typename state_type::storage_type;

    using node_ptr = typename state_type::node_ptr;
    using leaf_ptr = typename state_type::leaf_ptr;
    using internal_ptr = typename state_type::internal_ptr;

    using const_index_ptr = typename state_type::const_index_ptr;

    using list_summary = typename state_type::list_type::summary_type;

    /// Represents a complete subtree assembled by the bulk loading algorithm.
    /// This subtree will be inserted into the existing tree at the appropriate level.
    struct subtree_result {
        node_ptr root;
        size_t height;
        size_t size;

        subtree_result(node_ptr root, size_t height, size_t size)
            : root(root)
            , height(height)
            , size(size)
        {
            geodb_assert(height > 0, "invalid height");
            geodb_assert(size > 0, "empty subtree");
        }
    };

    /// The precomputed summary of a lower-level node.
    struct node_summary {
        node_ptr ptr{};
        bounding_box mbb;
        list_summary total;     ///< Summary of `index->total`
        u64 num_labels = 0;     ///< This many label summaries follow.

        template<typename Dst>
        friend void serialize(Dst& dst, const node_summary& n) {
            using tpie::serialize;
            serialize(dst, n.ptr);
            serialize(dst, n.mbb);
            serialize(dst, n.total);
            serialize(dst, n.num_labels);
        }

        template<typename Dst>
        friend void unserialize(Dst& dst, node_summary& n) {
            using tpie::unserialize;
            unserialize(dst, n.ptr);
            unserialize(dst, n.mbb);
            unserialize(dst, n.total);
            unserialize(dst, n.num_labels);
        }
    };

    /// The precomputed summary of a <label, postings_list> pair.
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
    /// Loads the given stream of leaf entries into the tree referenced by this loader.
    ///
    /// This is the main bulk loading function and should be called by the user.
    /// The implementation depends on the concrete subclass.
    void load(tpie::file_stream<tree_entry>& entries) {
        const u64 size = entries.size();

        if (size == 0) {
            return;
        }

        subtree_result result = derived()->load_impl(entries);
        geodb_assert(result.size == size, "must have loaded entire entry set");

        tree_insertion<state_type> inserter{state()};
        inserter.insert_node(result.root, result.height, size);
    }

protected:
    /// Writes the node summary of `leaf` to the given output file.
    void write_summary(tpie::serialization_writer& summaries, leaf_ptr leaf) {
        // Keep summaries for single leaves in memory.
        // The memory used for this is in O(leaf_size) and thus very small.
        std::vector<trajectory_id_type> all_ids;
        std::map<label_type, std::vector<trajectory_id_type>> label_ids;

        node_summary& node_sum = m_node_summary_buf;
        label_summary& label_sum = m_label_summary_buf;

        const u32 count = storage().get_count(leaf);
        for (u32 i = 0; i < count; ++i) {
            tree_entry entry = storage().get_data(leaf, i);

            all_ids.push_back(entry.trajectory_id);
            label_ids[entry.unit.label].push_back(entry.trajectory_id);
        }

        // Create a new node entry containing index information and mbb
        // and write it to the file.
        node_sum.ptr = leaf;
        node_sum.mbb = state().get_mbb(leaf);
        make_simple_summary(all_ids, count, node_sum.total);
        node_sum.num_labels = label_ids.size();
        summaries.serialize(node_sum);

        // Write a summary for each label.
        for (auto& pair : label_ids) {
            label_sum.label = pair.first;
            make_simple_summary(pair.second, pair.second.size(), label_sum.summary);
            summaries.serialize(label_sum);
        }
    }

    /// Writes the node summary of `internal` to the given output file.
    void write_summary(tpie::serialization_writer& summaries, internal_ptr internal) {
        node_summary& node_sum = m_node_summary_buf;
        label_summary& label_sum = m_label_summary_buf;

        const_index_ptr index = storage().const_index(internal);

        node_sum.ptr = internal;
        node_sum.mbb = state().get_mbb(internal);
        node_sum.total = index->total()->summarize();
        node_sum.num_labels = index->size();
        summaries.serialize(node_sum);

        // Summarize every label.
        for (auto entry : *index) {
            label_sum.label = entry.label();
            label_sum.summary = entry.postings_list()->summarize();
            summaries.serialize(label_sum);
        }
    }

    /// Reads a node summary from the given input file.
    /// The input file must be readable (i.e. not at the end).
    ///
    /// The NodeCallback will be invoked exactly once for the node_summary object.
    /// The LabelCallback will be invoked once for every of the following label_summary objects.
    template<typename NodeCallback, typename LabelCallback>
    void read_summary(tpie::serialization_reader& summaries, NodeCallback&& nc, LabelCallback&& lc) {
        geodb_assert(summaries.can_read(), "Input file is not readable");

        node_summary& node_sum = m_node_summary_buf;
        label_summary& label_sum = m_label_summary_buf;

        summaries.unserialize(node_sum);
        nc(const_cast<const node_summary&>(node_sum));

        for (u64 i = 0; i < node_sum.num_labels; ++i) {
            summaries.unserialize(label_sum);
            lc(const_cast<const label_summary&>(label_sum));
        }
    }

    /// Reads one node pointer from the given input file.
    /// The file must be readable.
    node_ptr read_root_ptr(tpie::serialization_reader& summaries) {
        node_ptr ptr{};
        read_summary(summaries, [&](const node_summary& ns) {
            ptr = ns.ptr;
        }, noop());
        return ptr;
    }

protected:
    tree_type& tree() const { return m_tree; }
    state_type& state() const { return tree().state; }
    storage_type& storage() const { return state().storage(); }

private:
    void make_simple_summary(std::vector<trajectory_id_type>& ids, u64 units, list_summary& out) {
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

        out.count = units;
        out.trajectories.assign(ids.begin(), ids.end());
    }

    Derived* derived() { return static_cast<Derived*>(this); }

protected:
    explicit bulk_load_common(tree_type& tree)
        : m_tree(tree)
    {}

private:
    tree_type& m_tree;
    node_summary m_node_summary_buf;
    label_summary m_label_summary_buf;
};

} // namespace geodb

#endif // GEODB_IRWI_BULK_LOAD_COMMON_HPP
