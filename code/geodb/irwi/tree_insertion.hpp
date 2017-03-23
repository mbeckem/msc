#ifndef GEODB_IRWI_TREE_INSERTION_HPP
#define GEODB_IRWI_TREE_INSERTION_HPP

#include "geodb/common.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/hybrid_buffer.hpp"
#include "geodb/hybrid_map.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/label_count.hpp"
#include "geodb/irwi/tree_partition.hpp"
#include "geodb/utility/function_utils.hpp"

#include <boost/container/static_vector.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <gsl/span>

#include <unordered_map>
#include <vector>

namespace geodb {

/// Implements the generic insertion algorithm for IRWI Trees.
template<typename State>
class tree_insertion {
    using storage_type = typename State::storage_type;

    using value_type = typename State::value_type;

    using id_set_type = typename State::id_set_type;

    using node_ptr = typename State::node_ptr;
    using leaf_ptr = typename State::leaf_ptr;
    using internal_ptr = typename State::internal_ptr;

    using index_type = typename State::index_type;
    using index_ptr = typename State::index_ptr;
    using const_index_ptr = typename State::const_index_ptr;

    using list_type = typename State::list_type;
    using list_ptr = typename State::list_ptr;
    using const_list_ptr = typename State::const_list_ptr;

    using list_summary_type = typename list_type::summary_type;

    using posting_type = typename State::posting_type;
    using posting_data_type = posting_data<State::lambda()>;

    using trajectory_id_set_type = typename posting_data_type::id_set_type;

    using partition_type = tree_partition<State>;
    using which_t = typename partition_type::which_t;
    using split_element = typename partition_type::split_element;
    using internal_entry = typename partition_type::internal_entry;

    template<typename Key, typename Value>
    using map_type = typename storage_type::template map_type<Key, Value>;

    template<typename Value>
    using buffer_type = typename storage_type::template buffer_type<Value>;

    /// The summary of a single label within a subtree of some node.
    /// Contains the label itself, the number of leaf entries with that label
    /// and a set that contains the ids of trajectories with that label.
    struct label_summary {
        label_type label = 0;
        posting_data_type data;

        label_summary() = default;

        label_summary(label_type label, u64 count, const id_set_type& ids)
            : label(label)
            , data(count, ids)
        {}
    };

    /// Summary of the subtree rooted at some node.
    struct node_summary {
        /// Pointer to the node.
        node_ptr ptr{};

        /// Bounding box that contains all items in the node's subtree.
        bounding_box mbb;

        /// The total count & trajectory id set for this subtree.
        posting_data_type total;

        /// A unit count & trajectory id set for every label
        /// in this node's subtree. Ordered by label (ascending).
        buffer_type<label_summary> labels;

        node_summary(storage_type& storage)
            : labels(storage.template make_buffer<label_summary>())
        {}

        node_summary(node_summary&&) noexcept = default;
        node_summary& operator=(node_summary&&) noexcept = default;
    };

    using internal_count_vec = boost::container::static_vector<u64, State::max_internal_entries()>;
    using internal_cost_vec = boost::container::static_vector<float, State::max_internal_entries()>;

private:
    State& state;
    storage_type& storage;

public:
    tree_insertion(State& state)
        : state(state), storage(state.storage())
    {}

    /// Same as the other overload, but creates a new vector instance every time.
    leaf_ptr traverse_tree(const value_type& v) {
        std::vector<internal_ptr> path;
        return traverse_tree(v, path);
    }

    /// Finds the appropriate leaf node for the insertion of \p d.
    /// Stores the path of internal node parents in \p path.
    ///
    /// Updates the child entries of all internal nodes on the way
    /// to reflect the insertion of `v`.
    ///
    /// \pre The tree is not empty.
    leaf_ptr traverse_tree(const value_type& v, std::vector<internal_ptr>& path) {
        geodb_assert(storage.get_height() > 0, "empty tree has no leaves");

        path.clear();
        if (storage.get_height() == 1) {
            // Root is the only leaf.
            return storage.to_leaf(storage.get_root());
        }

        // Iterate until the leaf level has been reached.
        internal_ptr current = storage.to_internal(storage.get_root());
        for (size_t level = 1;; ++level) {
            path.push_back(current);

            const u32 child_index = find_insertion_entry(current, v);
            update_parent(current, child_index, v);

            const node_ptr child = storage.get_child(current, child_index);
            if (level + 1 == storage.get_height()) {
                // Next level is the leaf level.
                return storage.to_leaf(child);
            }

            // Continue on the next level.
            current = storage.to_internal(child);
        }

        unreachable("must have reached the leaf level");
    }

    /// Same as the other overload, but creates a new vector instance every time.
    void insert(const value_type& v) {
        std::vector<internal_ptr> path;
        return insert(v, path);
    }

    /// Default insertion algorithm.
    /// Inserts the value `v` into the tree. `path` is used as a buffer
    /// to store the parents of the node where the insertion takes place.
    /// The parent nodes (and their inverted indices) are updated to reflect
    /// the insertion.
    void insert(const value_type& v, std::vector<internal_ptr>& path) {
        if (storage.get_height() == 0) {
            // The tree is empty, create the root node.
            leaf_ptr root = storage.create_leaf();
            insert_entry(root, v);
            storage.set_root(root);
            storage.set_height(1);
            storage.set_size(1);
            return;
        }

        leaf_ptr leaf = traverse_tree(v, path);
        storage.set_size(storage.get_size() + 1);
        if (storage.get_count(leaf) < State::max_leaf_entries()) {
            insert_entry(leaf, v);
            return;
        }

        insert_at_full(leaf, v, path);
    }

    /// Insert a new leaf node at some appropriate place in the tree.
    ///
    /// This function is used by the bulk loading algorithms to insert
    /// their result into an existing tree.
    void insert_node(leaf_ptr leaf) {
        insert_subtree(leaf, 1, storage.get_count(leaf));
    }

    /// Insert a subtree rooted at `internal` (with the given height and size)
    /// at some appropriate place inside the tree.
    /// The new subtree's structure must be compatible, i.e. it must look
    /// like it was created by this class.
    ///
    /// This function is used by the bulk loading algorithms to insert
    /// their result into an existing tree.
    void insert_node(internal_ptr internal, size_t height, size_t size) {
        geodb_assert(height > 1, "internal nodes have height > 1");
        insert_subtree(internal, height, size);
    }

private:
    /// Handles the simple edge cases of subtree insertion (e.g. the current tree
    /// is empty or has the same height as the new subtree).
    /// Dispatches all other cases to \ref insert_subtree_impl.
    void insert_subtree(node_ptr node, size_t height, size_t size) {
        storage.set_size(storage.get_size() + size);

        // If the tree is empty, simply set the new root.
        if (storage.get_height() == 0) {
            storage.set_height(height);
            storage.set_root(node);
            return;
        }

        // If they have the same height, create a new root with two entries.
        if (height == storage.get_height()) {
            internal_ptr new_root = storage.create_internal();
            insert_entry(new_root, summarize(storage.get_root(), height));
            insert_entry(new_root, summarize(node, height));
            storage.set_height(height + 1);
            storage.set_root(new_root);
            return;
        }

        // If the new subtree is higher than the current tree, swap them.
        if (height > storage.get_height()) {
            {
                node_ptr tmp = storage.get_root();
                storage.set_root(node);
                node = tmp;
            }
            {
                size_t tmp = storage.get_height();
                storage.set_height(height);
                height = tmp;
            }
        }

        insert_subtree_impl(summarize(node, height), height);
    }

    /// Inserts a subtree of height `child_height` into the current tree with greater height.
    /// At this point, the current tree's root is always an internal node.
    /// Loops until the correct level for insertion is reached, always chosing
    /// the path with the lowest cost.
    void insert_subtree_impl(const node_summary& child, size_t child_height)
    {
        geodb_assert(child_height < storage.get_height(), "subtree must be smaller");
        geodb_assert(storage.get_height() >= 1, "current root is always an internal node");

        // Contains the parents of `node`.
        // They will be revisited if a node split was necessary.
        std::vector<internal_ptr> path;

        // Descend into the tree until we are at the correct height to insert the new subtree.
        internal_ptr node = storage.to_internal(storage.get_root());
        size_t node_height = storage.get_height();
        while (node_height != child_height + 1) {
            path.push_back(node);

            auto label_counts = child.labels | transformed([](const label_summary& ls) {
                return label_count(ls.label, ls.data.count());
            });

            const u32 child_index = find_insertion_entry(node, child.mbb, label_counts, child.total.count());
            update_parent(node, child_index, child);

            node = storage.to_internal(storage.get_child(node, child_index));
            --node_height;
        }

        // Insert the new subtree into parent.
        if (storage.get_count(node) < State::max_internal_entries()) {
            insert_entry(node, child);
        } else {
            insert_at_full(node, child, path);
        }
    }

private:
    /// Inserts the new value at the given leaf, which must be full.
    /// Splits the leaf and its parents, if necessary.
    void insert_at_full(leaf_ptr leaf, const value_type& v, gsl::span<const internal_ptr> path) {
        geodb_assert(storage.get_count(leaf) == State::max_leaf_entries(), "leaf is not full");

        return handle_split(leaf, split_and_insert(leaf, v), path);
    }

    /// Inserts a new subtree into the given internal node, which must be full.
    /// Splits the node and its parents, if necessary.
    void insert_at_full(internal_ptr internal, const node_summary& child, gsl::span<const internal_ptr> path) {
        geodb_assert(storage.get_count(internal) == State::max_internal_entries(), "internal node is not full");

        return handle_split(internal, split_and_insert(internal, child), path);
    }

    /// Takes the result of a split operation (either at internal or leaf level)
    /// and updates the parents to reflect the change.
    /// The tree grows if the root was split.
    template<typename NodePointer>
    void handle_split(NodePointer old_node, NodePointer new_node, gsl::span<const internal_ptr> path) {
        node_summary old_summary = summarize(old_node);
        node_summary new_summary = summarize(new_node);

        // There are two nodes that have to be inserted (or updated)
        // at the next level.
        // The content of the old node has changed (because it was split),
        // thus its entry (including the index) within the parent must be replaced
        // unconditionally.
        // The new node might fit into the parent. In this case, insert an entry and return.
        // Otherwise, the parent must be split.
        while (!path.empty()) {
            internal_ptr parent = back(path);
            replace_entry(parent, old_summary);
            if (storage.get_count(parent) < State::max_internal_entries()) {
                insert_entry(parent, new_summary);
                return;
            }

            new_summary = summarize(split_and_insert(parent, new_summary));
            old_summary = summarize(parent);
            pop_back(path);
        }

        // Two nodes remain but there is no parent in path.
        // Create a new root and insert entries for both nodes.
        internal_ptr root = storage.create_internal();
        insert_entry(root, old_summary);
        insert_entry(root, new_summary);
        storage.set_root(root);
        storage.set_height(storage.get_height() + 1);
    }

    /// Update the parent node to reflect the insertion of `entry`.
    /// The bounding box for the given child and its inverted index entries will be expanded.
    void update_parent(internal_ptr parent, u32 child_index, const value_type& entry) {
        geodb_assert(child_index < storage.get_count(parent), "index out of bounds");

        const trajectory_id_type tid = state.get_id(entry);
        const bounding_box mbb = state.get_mbb(entry);
        const auto label_counts = state.get_label_counts(entry);
        const u64 total_count = state.get_total_count(entry);

        index_ptr index = storage.index(parent);
        storage.set_mbb(parent, child_index, storage.get_mbb(parent, child_index).extend(mbb));
        increment_entry(*index->total(), child_index, tid, total_count);
        for (const auto& lc : label_counts) {
            list_ptr list = index->find_or_create(lc.label)->postings_list();
            increment_entry(*list, child_index, tid, lc.count);
        }
    }

    /// Update the parent node to reflect the insertion of `entry` (which is a subtree)
    /// at the child with the given `child_index`.
    /// Updates the bounding box and the inverted index.
    void update_parent(internal_ptr parent, u32 child_index, const node_summary& entry) {
        geodb_assert(child_index < storage.get_count(parent), "index out of bounds");

        storage.set_mbb(parent, child_index, storage.get_mbb(parent, child_index).extend(entry.mbb));

        index_ptr index = storage.index(parent);
        increment_entry(*index->total(), child_index, entry.total.id_set(), entry.total.count());
        for (const label_summary& lc : entry.labels) {
            list_ptr list = index->find_or_create(lc.label)->postings_list();
            increment_entry(*list, child_index, lc.data.id_set(), lc.data.count());
        }
    }

    /// Update the entry for `child` in the given list or insert a new one.
    void increment_entry(list_type& list, entry_id_type child, trajectory_id_type tid, u64 count) {
        auto pos = list.find(child);
        if (pos == list.end()) {
            // First unit with that label. Make a new entry.
            list.append(posting_type(child, count, {tid}));
        } else {
            // Replace the entry with an updated version.
            posting_type e = *pos;

            auto set = e.id_set();
            set.add(tid);
            e.id_set(set);
            e.count(e.count() + count);

            list.set(pos, e);
        }
    }

    /// Update the entry for `child` in the given list or insert a new one.
    void increment_entry(list_type& list, entry_id_type child, const trajectory_id_set_type& tids, u64 count) {
        auto pos = list.find(child);
        if (pos == list.end()) {
            // Insert a new entry.
            list.append(posting_type(child, count, tids));
        } else {
            // Merge the new id set and count with the old content.
            posting_type e = *pos;

            auto set = e.id_set().union_with(tids);
            e.id_set(set);
            e.count(e.count() + count);

            list.set(pos, e);
        }
    }

    /// Simple case of `find_insertion_entry`: a single value.
    u32 find_insertion_entry(internal_ptr n, const value_type& v) {
        return find_insertion_entry(n, state.get_mbb(v), state.get_label_counts(v), state.get_total_count(v));
    }

    /// Given an internal node, and the mbb + labels of a new entry,
    /// find the best child entry within the internal node.
    /// This function respects the spatial cost (i.e. growing the bounding box)
    /// and the textual cost (introducing uncommon labels into a subtree).
    ///
    /// This function is more generic because it can also handle subtrees.
    template<typename LabelCounts>
    u32 find_insertion_entry(internal_ptr n,
                             const bounding_box& mbb,
                             const LabelCounts& labels,
                             u64 total_units)
    {
        const u32 count = storage.get_count(n);
        geodb_assert(count > 0, "empty internal node");

        internal_cost_vec textual, spatial;
        textual_insert_costs(n, labels, total_units, textual);
        spatial_insert_costs(n, mbb, spatial);

        auto cost = [&](u32 i) {
            return state.cost(spatial[i], textual[i]);
        };

        // Find the entry with the smallest cost.
        // Resolve ties by choosing the entry with the smallest mbb.
        u32 min_index = 0;
        float min_cost = cost(0);
        float min_size = storage.get_mbb(n, 0).size();
        for (u32 i = 1; i < count; ++i) {
            const float c = cost(i);
            const float size = state.get_mbb(n, i).size();
            if (c < min_cost || (c == min_cost && size < min_size)) {
                min_cost = c;
                min_index = i;
                min_size = size;
            }
        }
        return min_index;
    }

    /// Splits the leaf into two leaves and inserts the extra entry.
    /// Returns the new leaf.
    leaf_ptr split_and_insert(leaf_ptr old_leaf, const value_type& extra) {
        std::vector<value_type> entries = get_entries(old_leaf, extra);
        std::vector<split_element> split;
        partition_type(state).partition(entries, State::min_leaf_entries(), split);

        // Move the entries into place.
        auto assign_entries = [&](which_t part, leaf_ptr ptr) {
            // Copy the entries from the partition into the new node.
            u32 count = 0;
            for (const split_element& s : split) {
                if (s.which == part) {
                    storage.set_data(ptr, s.new_index, entries[s.old_index]);
                    ++count;
                }
            }
            storage.set_count(ptr, count);

            // Clear the remaining records.
            for (u32 i = count; i < State::max_leaf_entries(); ++i) {
                storage.set_data(ptr, i, value_type());
            }
        };

        leaf_ptr new_leaf = storage.create_leaf();
        assign_entries(partition_type::left, old_leaf);
        assign_entries(partition_type::right, new_leaf);
        return new_leaf;
    }

    /// Splits an internal node and inserts the new child entry.
    ///
    /// \param old_internal
    ///     The existing internal node (which must be full).
    /// \param extra
    ///     The new leaf or internal node that should be inserted,
    ///     represented by its summary.
    internal_ptr split_and_insert(internal_ptr old_internal, const node_summary& extra) {
        std::vector<internal_entry> entries = get_entries(old_internal, extra);
        std::vector<split_element> split;
        partition_type(state).partition(entries, State::min_internal_entries(), split);
        std::sort(split.begin(), split.end(), [&](const auto& a, const auto& b) {
            return a.old_index < b.old_index;
        });

        internal_ptr new_internal = storage.create_internal();
        apply_entry_split(old_internal, new_internal,
                          entries, split);
        apply_index_split(storage.index(old_internal),
                          storage.index(new_internal),
                          extra, split);

        return new_internal;
    }

    void apply_entry_split(internal_ptr old_internal, internal_ptr new_internal,
                           const std::vector<internal_entry>& entries,
                           const std::vector<split_element>& split)
    {
        // move entries indicated by p into the given node.
        auto assign_entries = [&](which_t which, internal_ptr ptr) {
            // Copy the entries from the part into the new node.
            u32 count = 0;
            for (const split_element& s : split) {
                if (s.which == which) {
                    storage.set_mbb(ptr, s.new_index, entries[s.old_index].mbb);
                    storage.set_child(ptr, s.new_index, entries[s.old_index].ptr);
                    ++count;
                }
            }
            storage.set_count(ptr, count);

            // Clear the remaining records.
            for (u32 i = count; i < State::max_internal_entries(); ++i) {
                storage.set_mbb(ptr, i, bounding_box());
                storage.set_child(ptr, i, node_ptr());
            }
        };

        assign_entries(partition_type::left, old_internal);
        assign_entries(partition_type::right, new_internal);
    }

    /// \pre split is sorted by old index.
    void apply_index_split(index_ptr old_index, index_ptr new_index,
                           const node_summary& extra,
                           const std::vector<split_element>& split)
    {
        std::vector<posting_type> result_left, result_right;
        result_left.reserve(State::max_internal_entries());
        result_right.reserve(State::max_internal_entries());
        auto split_entries = [&](list_ptr list) {
            result_left.clear();
            result_right.clear();

            for (const posting_type& entry : *list) {
                const u32 old_index = entry.node();
                const split_element& se = split[old_index];
                geodb_assert(se.old_index == old_index, "sorted by old index");

                posting_type new_entry(se.new_index, entry.count(), entry.id_set());

                switch (se.which) {
                case partition_type::left:
                    result_left.push_back(new_entry);
                    break;
                case partition_type::right:
                    result_right.push_back(new_entry);
                    break;
                default:
                    unreachable("Invalid part");
                    break;
                }
            }
        };

        // Move the postings lists into position.
        for (const auto& entry : *old_index) {
            const label_type label = entry.label();
            const auto old_list = entry.postings_list();

            split_entries(old_list);
            old_list->assign(result_left.begin(), result_left.end());
            if (!result_right.empty()) {
                auto new_list = new_index->create(label)->postings_list();
                new_list->assign(result_right.begin(), result_right.end());
            }
        }

        // Move the entries in "total" into  position.
        {
            auto old_total = old_index->total();
            auto new_total = new_index->total();
            split_entries(old_total);
            old_total->assign(result_left.begin(), result_left.end());
            new_total->assign(result_right.begin(), result_right.end());
        }

        // Add entries for the newly inserted node into the correct index.
        // The required entries can be calcuated by examining the already
        // existing summary.
        auto append_entries = [&](auto& index, entry_id_type id) {
            index->total()->append(posting_type(id, extra.total));
            for (const label_summary& ls : extra.labels) {
                index->find_or_create(ls.label)
                        ->postings_list()
                        ->append(posting_type(id, ls.data));
            }
        };

        const split_element& last = split.back();
        geodb_assert(last.old_index == State::max_internal_entries(), "last element is the extra one");

        switch (last.which) {
        case partition_type::left:
            append_entries(old_index, last.new_index);
            break;
        case partition_type::right:
            append_entries(new_index, last.new_index);
            break;
        default:
            unreachable("Node was not found in either part");
        }
    }

    /// Returns a vector that contains all entries of the given leaf plus
    /// the extra one.
    std::vector<value_type> get_entries(leaf_ptr leaf, const value_type& extra) {
        const u32 count = storage.get_count(leaf);

        std::vector<value_type> entries;
        entries.resize(count + 1);

        for (u32 i = 0; i < count; ++i) {
            entries[i] = storage.get_data(leaf, i);
        }
        entries[count] = extra;
        return entries;
    }

    /// Returns a vector that contains all (indexed) child entries of the given internal node
    /// and the extra node.
    std::vector<internal_entry> get_entries(internal_ptr internal, const node_summary& extra)
    {
        const u32 count = storage.get_count(internal);

        std::vector<internal_entry> entries;
        entries.reserve(count + 1);
        repeat(count + 1, [&]{ entries.emplace_back(storage); });

        for (u32 i = 0; i < count; ++i) {
            internal_entry& e = entries[i];
            e.ptr = storage.get_child(internal, i);
            e.mbb = storage.get_mbb(internal, i);
        }

        {
            // Open the inverted index and get the <label, count> pairs
            // for all already existing entries.
            auto index = storage.const_index(internal);

            // Gather totals.
            auto total = index->total();
            for (const posting_type& p : *total) {
                internal_entry& e = entries[p.node()];
                e.total = p.count();
            }

            // Gather the counts for the other labels.
            for (auto&& entry : *index) {
                const label_type label = entry.label();
                const_list_ptr list = entry.postings_list();

                // Do not propagate empty lists.
                if (list->size() > 0) {
                    for (const posting_type& p : *list) {
                        internal_entry& e = entries[p.node()];
                        e.labels.append(label_count(label, p.count()));
                    }
                }
            }
        }

        // Add the new entry as well.
        internal_entry& last = entries.back();
        last.ptr = extra.ptr;
        last.mbb = extra.mbb;
        for (const label_summary& ls : extra.labels) {
            last.labels.append(label_count(ls.label, ls.data.count()));
        }
        last.total = extra.total.count();

        return entries;
    }

    node_summary summarize(node_ptr n, size_t height) const {
        geodb_assert(height >= 1, "invalid subtree height");
        return height == 1
                ? summarize(storage.to_leaf(n))
                : summarize(storage.to_internal(n));
    }

    /// Summarizes a leaf node. This summary will become part of
    /// the new parent's inverted index.
    node_summary summarize(leaf_ptr n) const {
        const u32 count = storage.get_count(n);

        list_summary_type total;
        std::map<label_type, list_summary_type> labels;

        for (u32 i = 0 ; i < count; ++i) {
            value_type v = storage.get_data(n, i);
            const trajectory_id_type tid = state.get_id(v);
            const auto label_counts = state.get_label_counts(v);
            const u64 total_count = state.get_total_count(v);

            total.trajectories.add(tid);
            total.count += total_count;

            for (const auto& lc : label_counts) {
                list_summary_type& entry = labels[lc.label];

                entry.trajectories.add(tid);
                entry.count += lc.count;
            }
        }

        node_summary summary(storage);
        summary.ptr = n;
        summary.mbb = state.get_mbb(n);
        summary.total.count(total.count);
        summary.total.id_set(total.trajectories);
        for (auto& pair : labels) {
            const label_type label = pair.first;
            const u64 count = pair.second.count;
            const id_set_type& ids = pair.second.trajectories;
            summary.labels.append(label_summary(label, count, ids));
        }
        return summary;
    }

    /// Summarizes an internal node by summing the entries of
    /// all postings lists in its inverted index.
    /// This summary will become part of the new parent's inverted index.
    node_summary summarize(internal_ptr n) const {
        auto index = storage.const_index(n);

        auto to_posting_data = [&](const auto& list) {
            auto sum = list->summarize();
            return posting_data_type(sum.count, sum.trajectories);
        };

        node_summary summary(storage);
        summary.ptr = n;
        summary.mbb = state.get_mbb(n);
        summary.total = to_posting_data(index->total());
        for (const auto& entry : *index) {
            label_summary ls;
            ls.label = entry.label();
            ls.data = to_posting_data(entry.postings_list());

            if (ls.data.count()) {
                summary.labels.append(ls);
            }
        }
        return summary;
    }

public:
    /// Insert a data entry into a leaf.
    ///
    /// The leaf must not be full.
    ///
    /// (Public for unit testing).
    void insert_entry(leaf_ptr leaf, const value_type& e) {
        const u32 count = storage.get_count(leaf);
        geodb_assert(count < State::max_leaf_entries(), "leaf node is full");

        storage.set_data(leaf, count, e);
        storage.set_count(leaf, count + 1);
    }

    /// Inserts a new child into the given internal node.
    ///
    /// The node must not be full.
    /// The child's height must satisfy the tree's invariants.
    /// (Public for unit testing).
    template<typename NodePointer>
    void insert_entry(internal_ptr internal, NodePointer child) {
        insert_entry(internal, summarize(child));
    }

private:
    /// Inserts a new entry into the internal node `p` that references the
    /// node `c`. `p` must not be full.
    /// The inverted index of `p` will be updated to reflect the insertion of `c`.
    ///
    /// \param p    The parent node.
    /// \param c    The new child node (either a leaf or an internal node),
    ///             represented by its summary.
    void insert_entry(internal_ptr p, const node_summary& c) {
        const u32 i = storage.get_count(p);
        geodb_assert(i < State::max_internal_entries(), "internal node is full");

        storage.set_count(p, i + 1);
        storage.set_mbb(p, i, c.mbb);
        storage.set_child(p, i, c.ptr);

        index_ptr index = storage.index(p);
        insert_index(*index, i, c);
    }

    /// Reinserts a node into its parent.
    /// This function is called when a node `c` has been split.
    /// The parent node `p` will be updated to reflect the changes to `c`.
    ///
    /// \param p    The parent node.
    /// \param c    The child node. The child must already be referenced by `p`.
    ///             Can be either a leaf or an internal node.
    void replace_entry(internal_ptr p, const node_summary& c) {
        const u32 i = state.index_of(p, c.ptr);
        index_ptr index = storage.index(p);

        clear_index(*index, i);
        insert_index(*index, i, c);
        storage.set_mbb(p, i, c.mbb);
    }

    /// Insert the child node (represented by its summary)
    /// into the inverted index of ìts new parent.
    ///
    /// \param parent_index     The parent's inverted index.
    /// \param i                The index of `c` in its parent.
    /// \param c                The summary of the new child.
    void insert_index(index_type& parent_index, u32 i, const node_summary& c) {
        // Update the postings lists for all labels that occur in the summary.
        for (const label_summary& ls : c.labels) {
            if (ls.data.count() > 0) {
                parent_index.find_or_create(ls.label)
                        ->postings_list()
                        ->append(posting_type(i, ls.data));
            }
        }
        parent_index.total()->append(posting_type(i, c.total));
    }

    /// Remove all references to the given child entry from the index.
    void clear_index(index_type& index, entry_id_type id) {
        for (auto entry : index) {
            list_ptr list = entry.postings_list();
            remove_posting(*list, id);
        }
        remove_posting(*index.total(), id);
    }

    /// Removes the posting with the given id from the given list.
    /// Does nothing if no such posting exists.
    void remove_posting(list_type& list, entry_id_type id) {
        auto pos = list.find(id);
        if (pos != list.end()) {
            list.remove(pos);
        }
    }

    /// Calculates the textual insertion costs for every child entry of `internal`.
    /// \param internal
    ///     The current internal node.
    /// \param value_label_counts
    ///     A sorted range of <label_id, unit_count> pairs for the new entry.
    /// \param value_total_count
    ///     The total number of trajecotry units in the new entry.
    /// \param[out] result
    ///     Will contain the cost values.
    template<typename LabelCounts>
    void textual_insert_costs(internal_ptr internal,
                              const LabelCounts& value_label_counts,
                              u64 value_total_count,
                              internal_cost_vec& result) {
        const u32 count = storage.get_count(internal);
        const_index_ptr index = storage.const_index(internal);

        internal_count_vec entry_total_counts, entry_label_counts;
        counts(*index->total(), count, entry_total_counts);

        result.resize(count);
        std::fill(result.begin(), result.end(), 0.f);

        // Iterate over the shared labels of `v` and the node.
        // This loop is currently O(m * log n) where m is the number of labels in `v`
        // and n is the number of labels in the current internal node's index.
        for (const auto& lc : value_label_counts) {
            auto pos = index->find(lc.label);
            if (pos == index->end()) {
                continue;
            }
            counts(*pos->postings_list(), count, entry_label_counts);

            for (u32 i = 0; i < count; ++i) {
                // Generalized cost function for subtrees.
                // Compute the relative frequency of units with this label
                // compared with all other units in the subtree if the new value
                // would be inserted in internal->entries[i].
                const u64 combined_count = lc.count + entry_label_counts[i];
                const u64 combined_total = value_total_count + entry_total_counts[i];
                const float relative = float(combined_count) / float(combined_total);
                result[i] = std::max(result[i], relative);
            }
        }

        // Invert the frequencies to get the cost.
        for (float& f : result) {
            f = 1.0f - f;
        }
    }

    /// Calculates the spatial insertion cost for every child entry of `internal`.
    /// \param internal
    ///     The current internal node.
    /// \param mbb
    ///     The bounding box of the new entry.
    /// \param[out] cost
    ///     Will contain the cost values.
    void spatial_insert_costs(internal_ptr internal, const bounding_box& mbb, internal_cost_vec& cost) {
        const u32 count = storage.get_count(internal);
        const float norm = state.inverse(state.max_enlargement(internal, mbb));

        cost.resize(count);
        for (u32 i = 0; i < count; ++i) {
            cost[i] = norm * state.enlargement(storage.get_mbb(internal, i), mbb);
        }
    }

    /// Counts trajectory units for every child entry of the current internal node.
    /// \param list
    ///     The current postings list.
    /// \param count
    ///     The current internal node's child count.
    /// \param[out] cost
    ///     Will contain the unit count for every child of the current node.
    ///     The type of count depends on the input list (either total units or label units).
    void counts(const list_type& list, u32 count, internal_count_vec& result) {
        result.clear();
        result.resize(count, 0);
        for (const posting_type& p : list) {
            geodb_assert(p.node() < count, "invalid count");
            result[p.node()] = p.count();
        }
    }

    template<typename T>
    T& back(const gsl::span<T>& span) {
        return span[span.size() - 1];
    }

    template<typename T>
    void pop_back(gsl::span<T>& span) {
        geodb_assert(!span.empty(), "span must not be empty");
        span = span.first(span.size() - 1);
    }
};

} // namespace geodb

#endif // GEODB_IRWI_TREE_INSERTION_HPP
