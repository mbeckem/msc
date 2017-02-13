#ifndef IRWI_TREE_INSERTION_HPP
#define IRWI_TREE_INSERTION_HPP

#include "geodb/common.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/tree_partition.hpp"

#include <boost/range/adaptor/reversed.hpp>

#include <unordered_map>
#include <vector>

namespace geodb {

/// Implements the generic insertion algorithm for IRWI Trees.
template<typename State>
class tree_insertion {
    using storage_type = typename State::storage_type;

    using id_set_type = typename State::id_set_type;

    using node_ptr = typename State::node_ptr;
    using leaf_ptr = typename State::leaf_ptr;
    using internal_ptr = typename State::internal_ptr;

    using index_ptr = typename State::index_ptr;
    using list_ptr = typename State::list_ptr;

    using posting_type = typename State::posting_type;

    using partition_type = tree_partition<State>;
    using node_part_type = typename partition_type::node_part;

    /// Summary of the inverted index of some node.
    /// Uses O(num_labels * Lambda) space.
    struct index_summary {
        id_set_type total_tids;
        u64 total_units = 0;
        std::unordered_map<label_type, u64> label_units;
        std::unordered_map<label_type, id_set_type> label_tids;
    };

private:
    State& state;
    storage_type& storage;

public:
    tree_insertion(State& state)
        : state(state), storage(state.storage())
    {}

    /// Finds the appropriate leaf node for the insertion of \p d.
    /// Stores the path of internal node parents into \p path.
    leaf_ptr find_leaf(std::vector<internal_ptr>& path, const tree_entry& e) {
        geodb_assert(storage.get_height() > 0, "empty tree has no leaves");

        path.clear();
        if (storage.get_height() == 1) {
            // Root is the only leaf.
            return storage.to_leaf(storage.get_root());
        }

        const bounding_box mbb = state.get_mbb(e);
        const label_type label = state.get_label(e);

        // Iterate until the leaf level has been reached.
        internal_ptr current = storage.to_internal(storage.get_root());
        for (size_t level = 1;; ++level) {
            path.push_back(current);

            const node_ptr child = find_insertion_entry(current, mbb, label);
            if (level + 1 == storage.get_height()) {
                // Next level is the leaf level.
                return storage.to_leaf(child);
            }

            // Continue on the next level.
            current = storage.to_internal(child);
        }

        unreachable("must have reached the leaf level");
    }

    /// Insert the entry at the given leaf with the given path.
    /// \param leaf
    ///     The target leaf.
    /// \param path
    ///     The parents of the leaf.
    /// \param e
    ///     The new entry.
    void insert(leaf_ptr leaf, std::vector<internal_ptr> path, const tree_entry& e) {
        storage.set_size(storage.get_size() + 1);

        {
            const u32 count = storage.get_count(leaf);
            if (count < State::max_leaf_entries()) {
                // The leaf has room for this entry.
                insert_entry(leaf, e);
                update_path(path, leaf, e);
                return;
            }
        }

        // The leaf is full. Split it and insert the new entry.
        const leaf_ptr new_leaf = split_and_insert(leaf, e);
        if (path.empty()) {
            // The root was a leaf and has been split.
            internal_ptr root = storage.create_internal();
            insert_entry(root, leaf);
            insert_entry(root, new_leaf);
            storage.set_root(root);
            storage.set_height(2);
            return;
        }

        // There was at least one internal node parent.
        // The content of l has changed, update the parent's mbb and inverted index.
        internal_ptr parent = path.back();
        replace_entry(parent, leaf);
        if (storage.get_count(parent) < State::max_internal_entries()) {
            // The direct parent has enough space to store the new leaf.
            // Insert the new leaf and notify the parents about the insertion
            // of `d`. p will be up-to-date because ll is going to be inserted
            // and l has been reinserted already.
            insert_entry(parent, new_leaf);
            path.pop_back();
            update_path(path, parent, e);
            return;
        }

        // At this point, we have two internal nodes that need to be inserted
        // into their parent. If there is no parent, then we create a new
        // internal node that becomes our new root.
        internal_ptr new_internal = split_and_insert(parent, new_leaf);
        internal_ptr old_internal = parent;
        path.pop_back();
        while (!path.empty()) {
            parent = path.back();
            replace_entry(parent, old_internal);
            if (storage.get_count(parent) < State::max_internal_entries()) {
                insert_entry(parent, new_internal);
                path.pop_back();
                update_path(path, parent, e);
                return;
            }

            path.pop_back();
            new_internal = split_and_insert(parent, new_internal);
            old_internal = parent;
        }

        // Two internal nodes remain but there is no parent in path:
        // We need a new root.
        internal_ptr root = storage.create_internal();
        insert_entry(root, old_internal);
        insert_entry(root, new_internal);
        storage.set_root(root);
        storage.set_height(storage.get_height() + 1);
    }

private:
    /// Given an internal node, and the mbb + label of a new entry,
    /// find the best child entry within the internal node.
    /// This function respects the spatial cost (i.e. growing the bounding box)
    /// and the textual cost (introducing uncommon labels into a subtree).
    node_ptr find_insertion_entry(internal_ptr n, const bounding_box& b, label_type label) {
        const u32 count = storage.get_count(n);
        geodb_assert(count > 0, "empty internal node");

        // open the postings lists and retrieve the unit counts for all entries.
        std::vector<u64> unit_count;    // Units with the current label
        std::vector<u64> total_count;   // Total units (includes other labels)
        {
            auto index = storage.const_index(n);
            total_count = index->total()->counts(count);

            auto list_pos = index->find(label);
            if (list_pos == index->end()) {
                unit_count.resize(count, 0);
            } else {
                unit_count = list_pos->postings_list()->counts(count);
            }
        }

        // Combined cost function using weight parameter beta.
        const float norm = state.inverse(state.max_enlargement(n, b));
        const auto cost = [&](u32 index) {
            const float spatial = state.spatial_cost(state.get_mbb(n, index), b, norm);
            const float textual = state.textual_cost(unit_count[index], total_count[index]);
            return state.cost(spatial, textual);
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
        return storage.get_child(n, min_index);
    }

    /// Splits the leaf into two leaves and inserts the extra entry.
    /// Returns the new leaf.
    leaf_ptr split_and_insert(leaf_ptr old_leaf, const tree_entry& extra) {
        partition_type part(state);

        typename partition_type::overflowing_leaf_node o;
        part.create(o, old_leaf, extra);

        // Partition the entries of o.
        node_part_type left, right;
        part.partition(o, left, right);

        // Move the entries into place.
        auto assign_entries = [&](node_part_type& part, leaf_ptr ptr) {
            // Copy the entries from the partition into the new node.
            u32 count = 0;
            for (auto index : part.entries) {
                storage.set_data(ptr, count++, o.entries[index]);
            }
            storage.set_count(ptr, count);

            // Clear the remaining records.
            for (u32 i = count; i < State::max_leaf_entries(); ++i) {
                storage.set_data(ptr, i, tree_entry());
            }
        };

        leaf_ptr new_leaf = storage.create_leaf();
        assign_entries(left, old_leaf);
        assign_entries(right, new_leaf);
        return new_leaf;
    }

    /// Splits an internal node and inserts the new child entry.
    ///
    /// \param old_internal
    ///     The existing internal node (which must be full).
    /// \param c
    ///     The new leaf or internal node that should be inserted.
    template<typename NodePointer>
    internal_ptr split_and_insert(internal_ptr old_internal, NodePointer c) {
        internal_ptr new_internal = storage.create_internal();
        const auto sum = summarize(c);

        partition_type part(state);
        typename partition_type::overflowing_internal_node o;
        part.create(o, old_internal, c, sum);

        node_part_type left, right;
        part.partition(o, left, right);

        // old index -> new index. does not say in which node entries are now.
        std::map<u32, u32> index_map;

        // move entries indicated by p into the given node.
        auto assign_entries = [&](node_part_type& part, internal_ptr ptr) {
            // Copy the entries from the part into the new node.
            u32 count = 0;
            for (auto index : part.entries) {
                storage.set_mbb(ptr, count, o.entries[index].mbb);
                storage.set_child(ptr, count, o.entries[index].ptr);
                index_map[index] = count;
                ++count;
            }
            storage.set_count(ptr, count);

            // Clear the remaining records.
            for (u32 i = count; i < State::max_internal_entries(); ++i) {
                storage.set_mbb(ptr, i, bounding_box());
                storage.set_child(ptr, i, node_ptr());
            }
        };

        assign_entries(left, old_internal);
        assign_entries(right, new_internal);

        // move_list takes the postings list entries from the given list
        // and stores them in result_left and result_right.
        std::vector<posting_type> result_left, result_right;
        auto split_entries = [&](auto&& list) {
            result_left.clear();
            result_right.clear();

            for (const auto& entry : *list) {
                posting_type p(index_map[entry.node()], entry.count(), entry.id_set());
                if (left.entries.count(entry.node())) {
                    result_left.push_back(p);
                } else {
                    geodb_assert(right.entries.count(entry.node()), "Must be in the other partition");
                    result_right.push_back(p);
                }
            }
        };

        // Iterate over all postings lists and move the entries.
        auto old_index = storage.index(old_internal);
        auto new_index = storage.index(new_internal);

        // Move the label entries into position.
        for (const auto& entry : *old_index) {
            const label_type label = entry.label();
            const auto old_list = entry.postings_list();

            // TODO: Erase if empty? Consider iterator invalidation.
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
            index->total()->append(posting_type(id, sum.total_units, sum.total_tids));
            // TODO: One map instead of 2.
            for (auto pair : sum.label_units) {
                index->find_or_create(pair.first)
                        ->postings_list()
                        ->append(posting_type(id, pair.second, sum.label_tids.at(pair.first)));
            }
        };
        if (auto old_position = state.optional_index_of(old_internal, c)) {
            append_entries(old_index, *old_position);
        } else if (auto new_position = state.optional_index_of(new_internal, c)) {
            append_entries(new_index, *new_position);
        } else {
            unreachable("Node was not found in either parent");
        }

        return new_internal;
    }

    /// Summarizes a leaf node. This summary will become part of
    /// the new parent's inverted index.
    index_summary summarize(leaf_ptr n) const {
        const u32 count = storage.get_count(n);

        index_summary s;
        for (u32 i = 0 ; i < count; ++i) {
            auto&& data = storage.get_data(n, i);
            const trajectory_id_type tid = state.get_id(data);
            const label_type label = state.get_label(data);

            s.total_tids.add(tid);
            ++s.total_units;

            s.label_tids[label].add(tid);
            ++s.label_units[label];
        }
        return s;
    }

    /// Summarizes an internal node by summing the entries of
    /// all postings lists in its inverted index.
    /// This summary will become part of the new parent's inverted index.
    index_summary summarize(internal_ptr n) const {
        auto sum_list = [&](const auto& list, id_set_type& ids, u64& count) {
            auto sum = list->summarize();
            ids = std::move(sum.trajectories);
            count = sum.count;
        };

        auto index = storage.const_index(n);

        index_summary s;
        sum_list(index->total(), s.total_tids, s.total_units);
        for (const auto& entry : *index) {
            const label_type label = entry.label();

            id_set_type tids;
            u64 count;
            sum_list(entry.postings_list(), tids, count);
            if (count > 0) {
                s.label_units[label] = count;
                s.label_tids[label] = std::move(tids);
            }
        }
        return s;
    }

    /// Insert a data entry into a leaf. The leaf must not be full.
    void insert_entry(leaf_ptr leaf, const tree_entry& e) {
        const u32 count = storage.get_count(leaf);
        geodb_assert(count < State::max_leaf_entries(), "leaf node is full");

        storage.set_data(leaf, count, e);
        storage.set_count(leaf, count + 1);
    }

    /// Inserts a new entry into the internal node `p` that references the
    /// node `c`. `p` must not be full.
    /// The inverted index of `p` will be updated to reflect the insertion of `c`.
    ///
    /// \param p    The parent node.
    /// \param c    The new child node (either a leaf or an internal node).
    template<typename NodePointer>
    void insert_entry(internal_ptr p, NodePointer c) {
        const u32 i = storage.get_count(p);
        geodb_assert(i < State::max_internal_entries(), "internal node is full");

        storage.set_count(p, i + 1);
        storage.set_mbb(p, i, state.get_mbb(c));
        storage.set_child(p, i, c);

        index_ptr index = storage.index(p);
        const auto sum = summarize(c);

        // Update the postings lists for all labels that occur in the summary.
        // TODO: One map isntead of 2
        for (auto& pair : sum.label_units) {
            label_type label = pair.first;
            u64 count = pair.second;
            if (count > 0) {
                index->find_or_create(label)
                        ->postings_list()
                        ->append(posting_type(i, count, sum.label_tids.at(label)));
            }
        }
        index->total()->append(posting_type(i, sum.total_units, sum.total_tids));
    }

    /// Reinserts a node into its parent.
    /// This function is called when a node `c` has been split.
    /// The parent node `p` will be updated to reflect the changes to `c`.
    ///
    /// \param p    The parent node.
    /// \param c    The child node. The child must already be referenced by `p`.
    ///             Can be either a leaf or an internal node.
    template<typename NodePointer>
    void replace_entry(internal_ptr p, NodePointer c) {
        const u32 id = state.index_of(p, c);
        const index_summary sum = summarize(c);
        const index_ptr index = storage.index(p);

        // Adjust the mbb to fit the node's current entry set.
        storage.set_mbb(p, id, state.get_mbb(c));

        // Erase all posting list entries for labels that do not occur in c anymore.
        for (auto i = index->begin(), e = index->end(); i != e; ++i) {
            const label_type label = i->label();

            if (sum.label_units.find(label) == sum.label_units.end()) {
                const auto list = i->postings_list();
                const auto pos = list->find(id);
                if (pos != list->end()) {
                    list->remove(pos);
                }
            }
        }

        // Update the entries for labels in 'c'.
        // A label might be new if it was new with the unit that caused the node to split.
        // TODO: 1 map
        for (auto pair : sum.label_units) {
            label_type label = pair.first;
            u64 count = pair.second;
            update_entry(index, label, posting_type(id, count, sum.label_tids.at(label)));
        }
        update_entry(index->total(), posting_type(id, sum.total_units, sum.total_tids));
    }

    /// Update every parent node in the path with the fact that a new
    /// unit has been inserted into the given child, with the provided mbb
    /// and label id.
    void update_path(const std::vector<internal_ptr>& path, node_ptr child, const tree_entry& e)
    {
        const trajectory_id_type tid = state.get_id(e);
        const bounding_box unit_mbb = state.get_mbb(e);
        const label_type label = state.get_label(e);

        // Increment the unit count for the given node & label.
        auto increment_entry = [&](const list_ptr& list, entry_id_type node) {
            auto pos = list->find(node);
            if (pos == list->end()) {
                // First unit with that label. Make a new entry.
                list->append(posting_type(node, 1, {tid}));
            } else {
                // Replace the entry with an updated version.
                posting_type e = *pos;

                auto set = e.id_set();
                set.add(tid);
                e.id_set(set);
                e.count(e.count() + 1);

                list->set(pos, e);
            }
        };

        // Update mbbs and inverted indices in all parents.
        // The mbb of the child's entry within the parent must fit the new box.
        for (auto parent : path | boost::adaptors::reversed) {
            const u32 i = state.index_of(parent, child);
            bounding_box mbb = storage.get_mbb(parent, i).extend(unit_mbb);
            storage.set_mbb(parent, i, mbb);

            index_ptr index = storage.index(parent);
            increment_entry(index->total(), i);
            increment_entry(index->find_or_create(label)->postings_list(), i);

            child = parent;
        }
    }

    /// Updates the posting list entry for the given label. Creates the index entry if necessary.
    void update_entry(const index_ptr& index, label_type label, const posting_type& e) {
        const auto iter = index->find_or_create(label);
        const auto list = iter->postings_list();
        update_entry(list, e);
    }

    /// Updates the entry in `list` for the node `e.node()`.
    /// Inserts the entry at the end if no entry for this node existed.
    void update_entry(const list_ptr& list, const posting_type& e) {
        const auto pos = list->find(e.node());
        if (pos != list->end()) {
            list->set(pos, e);
        } else {
            list->append(e);
        }
    }
};


} // namespace geodb

#endif // IRWI_TREE_INSERTION_HPP
