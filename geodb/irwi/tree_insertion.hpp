#ifndef GEODB_IRWI_TREE_INSERTION_HPP
#define GEODB_IRWI_TREE_INSERTION_HPP

#include "geodb/common.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/tree_partition.hpp"

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

    using posting_type = typename State::posting_type;

    using partition_type = tree_partition<State>;
    using which_t = typename partition_type::which_t;
    using split_element = typename partition_type::split_element;
    using internal_entry = typename partition_type::internal_entry;

    /// Summary of the inverted index of some node.
    /// Uses O(num_labels * Lambda) space.
    // TODO: Use external storage if the number of labels is very large.
    struct index_summary {
        id_set_type total_tids;
        u64 total_units = 0;
        std::unordered_map<label_type, u64> label_units;
        std::unordered_map<label_type, id_set_type> label_tids;
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

    /// Finds the appropriate leaf node for the insertion of \p d.
    /// Stores the path of internal node parents in \p path.
    ///
    /// \pre Thre tree is not empty.
    leaf_ptr find_leaf(const value_type& v, std::vector<internal_ptr>& path) {
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

            const node_ptr child = find_insertion_entry(current, v);
            if (level + 1 == storage.get_height()) {
                // Next level is the leaf level.
                return storage.to_leaf(child);
            }

            // Continue on the next level.
            current = storage.to_internal(child);
        }

        unreachable("must have reached the leaf level");
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

        leaf_ptr leaf = find_leaf(v, path);
        insert_at_leaf(leaf, v, path);
    }

private:
    void insert_at_leaf(leaf_ptr leaf, const value_type& v, gsl::span<const internal_ptr> path) {
        storage.set_size(storage.get_size() + 1);
        {
            const u32 count = storage.get_count(leaf);
            if (count < State::max_leaf_entries()) {
                // The leaf has room for this entry.
                insert_entry(leaf, v);
                update_path(path, leaf, v);
                return;
            }
        }

        // The leaf is full. Split it and insert the new entry.
        const leaf_ptr new_leaf = split_and_insert(leaf, v);
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
        internal_ptr parent = back(path);
        replace_entry(parent, leaf);
        if (storage.get_count(parent) < State::max_internal_entries()) {
            // The direct parent has enough space to store the new leaf.
            // Insert the new leaf and notify the parents about the insertion
            // of `d`. p will be up-to-date because ll is going to be inserted
            // and l has been reinserted already.
            insert_entry(parent, new_leaf);
            pop_back(path);
            update_path(path, parent, v);
            return;
        }

        // At this point, we have two internal nodes that need to be inserted
        // into their parent. If there is no parent, then we create a new
        // internal node that becomes our new root.
        internal_ptr new_internal = split_and_insert(parent, new_leaf);
        internal_ptr old_internal = parent;
        pop_back(path);
        while (!path.empty()) {
            parent = back(path);
            replace_entry(parent, old_internal);
            if (storage.get_count(parent) < State::max_internal_entries()) {
                insert_entry(parent, new_internal);
                pop_back(path);
                update_path(path, parent, v);
                return;
            }

            pop_back(path);
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

public:
    /// Update every parent node in the path with the fact that a new
    /// leaf value has been inserted. This function will update all bounding boxes
    /// and postings lists on its way to the root.
    void update_path(gsl::span<const internal_ptr> path, node_ptr child, const value_type& v)
    {
        const trajectory_id_type tid = state.get_id(v);
        const bounding_box mbb = state.get_mbb(v);
        const auto label_counts = state.get_label_counts(v);
        const u64 total_count = state.get_total_count(v);

        // Increment the unit count for the given node & label.
        auto increment_entry = [&](const list_ptr& list, entry_id_type node, u64 count) {
            auto pos = list->find(node);
            if (pos == list->end()) {
                // First unit with that label. Make a new entry.
                list->append(posting_type(node, count, {tid}));
            } else {
                // Replace the entry with an updated version.
                posting_type e = *pos;

                auto set = e.id_set();
                set.add(tid);
                e.id_set(set);
                e.count(e.count() + count);

                list->set(pos, e);
            }
        };

        // Update mbbs and inverted indices in all parents.
        // The mbb of the child's entry within the parent must fit the new box.
        for (auto parent : path | boost::adaptors::reversed) {
            const u32 i = state.index_of(parent, child);
            bounding_box new_mbb = storage.get_mbb(parent, i).extend(mbb);
            storage.set_mbb(parent, i, new_mbb);

            index_ptr index = storage.index(parent);
            increment_entry(index->total(), i, total_count);
            for (const auto& lc : label_counts) {
                increment_entry(index->find_or_create(lc.label)->postings_list(), i, lc.count);
            }
            child = parent;
        }
    }

private:
    /// Given an internal node, and the mbb + label of a new entry,
    /// find the best child entry within the internal node.
    /// This function respects the spatial cost (i.e. growing the bounding box)
    /// and the textual cost (introducing uncommon labels into a subtree).
    node_ptr find_insertion_entry(internal_ptr n, const value_type& v) {
        const u32 count = storage.get_count(n);
        geodb_assert(count > 0, "empty internal node");

        internal_cost_vec textual, spatial;
        textual_insert_costs(n, v, textual);
        spatial_insert_costs(n, v, spatial);

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
        return storage.get_child(n, min_index);
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
    ///     The new leaf or internal node that should be inserted.
    template<typename NodePointer>
    internal_ptr split_and_insert(internal_ptr old_internal, NodePointer extra) {
        const index_summary summary = summarize(extra);

        std::vector<internal_entry> entries = get_entries(old_internal, extra, summary);
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
                          summary, split);

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
                           const index_summary& summary,
                           const std::vector<split_element>& split)
    {
        std::vector<posting_type> result_left, result_right;
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
            index->total()->append(posting_type(id, summary.total_units, summary.total_tids));
            // TODO: One map instead of 2.
            for (auto pair : summary.label_units) {
                index->find_or_create(pair.first)
                        ->postings_list()
                        ->append(posting_type(id, pair.second, summary.label_tids.at(pair.first)));
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
    template<typename NodePointer>
    std::vector<internal_entry> get_entries(internal_ptr internal, NodePointer extra, const index_summary& summary)
    {
        const u32 count = storage.get_count(internal);

        std::vector<internal_entry> entries;
        entries.resize(count + 1);

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
                e.total_units = p.count();
            }

            // Gather the counts for the other labels.
            for (auto&& entry : *index) {
                const label_type label = entry.label();
                const_list_ptr list = entry.postings_list();

                // Do not propagate empty lists.
                if (list->size() > 0) {
                    for (const posting_type& p : *list) {
                        internal_entry& e = entries[p.node()];
                        e.label_units[label] = p.count();
                    }
                }
            }
        }

        // Add the new entry as well.
        internal_entry& last = entries.back();
        last.ptr = extra;
        last.mbb = state.get_mbb(extra);
        last.label_units.insert(summary.label_units.begin(), summary.label_units.end());
        last.total_units = summary.total_units;

        return entries;
    }

    /// Summarizes a leaf node. This summary will become part of
    /// the new parent's inverted index.
    index_summary summarize(leaf_ptr n) const {
        const u32 count = storage.get_count(n);

        index_summary s;
        for (u32 i = 0 ; i < count; ++i) {
            value_type v = storage.get_data(n, i);
            const trajectory_id_type tid = state.get_id(v);
            const auto label_counts = state.get_label_counts(v);
            const u64 total_count = state.get_total_count(v);

            s.total_tids.add(tid);
            s.total_units += total_count;

            for (const auto& lc : label_counts) {
                s.label_tids[lc.label].add(tid);
                s.label_units[lc.label] += lc.count;
            }
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
    void insert_entry(leaf_ptr leaf, const value_type& e) {
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

    /// Calculates the textual insertion costs for every child entry of `internal`.
    /// \param internal
    ///     The current internal node.
    /// \param v
    ///     The new leaf entry.
    /// \param[out] result
    ///     Will contain the cost values.
    void textual_insert_costs(internal_ptr internal, const value_type& v, internal_cost_vec& result) {
        const u32 count = storage.get_count(internal);
        const_index_ptr index = storage.const_index(internal);

        const auto value_label_counts = state.get_label_counts(v);
        const u64 value_total_count = state.get_total_count(v);

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
    /// \param v
    ///     The new leaf entry.
    /// \param[out] cost
    ///     Will contain the cost values.
    void spatial_insert_costs(internal_ptr internal, const value_type& v, internal_cost_vec& cost) {
        const bounding_box mbb = state.get_mbb(v);
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
