#ifndef IRWI_TREE_PARTITION_HPP
#define IRWI_TREE_PARTITION_HPP

#include "geodb/common.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/posting.hpp"
#include "geodb/utility/range_utils.hpp"

#include <boost/container/static_vector.hpp>

#include <array>
#include <map>
#include <unordered_set>

namespace geodb {

/// Implements the generic node splitting algorithm for IRWI trees.
template<typename State>
class tree_partition {
    using storage_type = typename State::storage_type;

    using id_set_type = typename State::id_set_type;

    using node_ptr = typename State::node_ptr;
    using leaf_ptr = typename State::leaf_ptr;
    using internal_ptr = typename State::internal_ptr;

    using posting_type = typename State::posting_type;

    /// A label id and a count of trajectory units.
    struct label_count {
        label_type label;
        u64 count;
    };

private:
    State& state;
    storage_type& storage;

public:
    tree_partition(State& state)
        : state(state)
        , storage(state.storage())
    {}

    /// Represents a leaf node that is currently overflowing.
    struct overflowing_leaf_node : boost::noncopyable {
        static constexpr size_t max_entries = State::max_leaf_entries();
        static constexpr size_t min_entries = State::min_leaf_entries();
        static constexpr size_t N = max_entries + 1;

        /// Exactly N entries since this node is overflowing.
        boost::container::static_vector<tree_entry, N> entries;

        /// All labels in this node.
        std::unordered_set<label_type> labels;
    };

    /// Construct a virtual node that holds all existing entries
    /// and the new one.
    void create(overflowing_leaf_node& o, leaf_ptr n, const tree_entry& extra) {
        geodb_assert(o.entries.size() == 0, "o must be fresh");
        geodb_assert(storage.get_count(n) == State::max_leaf_entries(), "n must be full");
        for (u32 i = 0; i < u32(State::max_leaf_entries()); ++i) {
            tree_entry d = storage.get_data(n, i);
            o.entries.push_back(d);
            o.labels.insert(state.get_label(d));
        }
        o.entries.push_back(extra);
        o.labels.insert(state.get_label(extra));
    }

    bounding_box get_mbb(const overflowing_leaf_node& n, u32 index) const {
        return state.get_mbb(n.entries[index]);
    }

    u32 get_count(const overflowing_leaf_node& n) const {
        geodb_assert(n.entries.size() == n.N, "node must be full");
        return n.N;
    }

    auto get_labels(const overflowing_leaf_node& n, u32 index) const {
        return std::array<label_count, 1>{{state.get_label(n.entries[index]), 1}};
    }

    u64 get_total_units(const overflowing_leaf_node& n, u32 index) const {
        geodb_assert(index < n.entries.size(), "index out of bounds");
        unused(n, index);
        return 1;
    }

    /// Represents an internal node that is currently overflowing.
    struct overflowing_internal_node : boost::noncopyable {
        static constexpr size_t max_entries = State::max_internal_entries();
        static constexpr size_t min_entries = State::min_internal_entries();
        static constexpr size_t N = max_entries + 1;

        struct child_data {
            node_ptr ptr;
            bounding_box mbb;
            std::map<label_type, u64> label_units;          // label -> unit count
            // std::map<label_type, id_set_type> label_tids;   // label -> trajectory id set
            id_set_type total_tids;
            u64 total_units = 0;
        };

        /// Exactly N entries.
        boost::container::static_vector<child_data, N> entries;
        std::unordered_set<label_type> labels;
    };

    /// Create a new virtual node that holds all existing internal entries of `n`
    /// and the additional new child.
    template<typename NodePointer, typename NodeSummary>
    void create(overflowing_internal_node& o, internal_ptr n, NodePointer child, const NodeSummary& child_summary)
    {
        geodb_assert(o.entries.size() == 0, "o must be fresh");
        geodb_assert(storage.get_count(n) == State::max_internal_entries(), "n must be full");

        for (u32 i = 0; i < u32(State::max_internal_entries()); ++i) {
            o.entries.emplace_back();
            auto& entry = o.entries.back();
            entry.ptr = storage.get_child(n, i);
            entry.mbb = storage.get_mbb(n, i);
        }

        {
            // Open the inverted index and get the <label, count> pairs
            // for all already existing entries.
            auto index = storage.const_index(n);

            // Gather totals.
            auto total = index->total();
            for (const posting_type& p : *total) {
                auto& entry = o.entries[p.node()];
                entry.total_units = p.count();
                entry.total_tids = p.id_set();
            }

            // Gather the counts for the other labels.
            for (auto&& entry : *index) {
                const label_type label = entry.label();
                auto list = entry.postings_list();

                // Do not propagate empty lists.
                if (list->size() > 0) {
                    for (const posting_type& p : *list) {
                        auto& entry = o.entries[p.node()];
                        entry.label_units[label] = p.count();
                    }
                    o.labels.insert(label);
                }
            }
        }

        // Add the new entry as well.
        o.entries.emplace_back();
        auto& entry = o.entries.back();
        entry.ptr = child;
        entry.mbb = state.get_mbb(child);
        entry.total_tids = child_summary.total_tids;
        entry.total_units = child_summary.total_units;
        entry.label_units.insert(child_summary.label_units.begin(), child_summary.label_units.end());
        for (auto& pair : entry.label_units) {
            if (pair.second > 0) {
                o.labels.insert(pair.first);
            }
        }
    }

    bounding_box get_mbb(const overflowing_internal_node& o, u32 index) const {
        return o.entries[index].mbb;
    }

    u64 get_total_units(const overflowing_internal_node& o, u32 index) const {
        return o.entries[index].total_units;
    }

    auto get_labels(const overflowing_internal_node& o, u32 index) const {
        auto transform = [](const auto& pair) {
            return label_count{pair.first, pair.second};
        };
        return o.entries[index].label_units | transformed(transform);
    }

    u32 get_count(const overflowing_internal_node& o) const {
        geodb_assert(o.entries.size() == o.N, "node must be full");
        return o.N;
    }

    struct node_part : boost::noncopyable {
        /// Minimal bounding box for all entries.
        bounding_box mbb;

        /// Indices into the overflowing node.
        std::unordered_set<u32> entries;

        /// Map of label -> count.
        std::map<label_type, u64> labels;

        const bounding_box& get_mbb() const {
            return mbb;
        }

        auto get_labels() const {
            auto transform = [](const auto& pair) {
                return label_count{pair.first, pair.second};
            };
            return labels | transformed(transform);
        }

        u64 get_total_units() const {
            return entries.size();
        }

        template<typename LabelCounts>
        void init(u32 i, const bounding_box& b, const LabelCounts& label_counts) {
            entries.insert(i);
            mbb = b;
            for (const label_count& lc : label_counts) {
                labels[lc.label] += lc.count;
            }
        }

        template<typename LabelCounts>
        void add(u32 i, const bounding_box& b, const LabelCounts& label_counts) {
            entries.insert(i);
            mbb = mbb.extend(b);
            for (const label_count& lc : label_counts) {
                labels[lc.label] += lc.count;
            }
        }

        size_t size() const {
            return entries.size();
        }
    };

    /// Partitions the entries of a single node into two groups.
    /// Implements the quadradic node splitting algorithm for R-Trees
    /// (extended for spatio-textual trajectories).
    ///
    /// \param n            The overflowing node.
    /// \param left         The left partition.
    /// \param right        The right partition.
    template<typename OverflowingNode, typename Part>
    void partition(const OverflowingNode& n, Part& left, Part& right) {
        static constexpr size_t max_entries = OverflowingNode::max_entries;
        static constexpr size_t min_entries = OverflowingNode::min_entries;
        static constexpr size_t N = max_entries + 1;

        geodb_assert(get_count(n) == N, "node must be full");
        geodb_assert(left.size() == 0, "left group must be empty");
        geodb_assert(right.size() == 0, "right group must be empty");

        u32 left_seed, right_seed;
        std::tie(left_seed, right_seed) = pick_seeds(n);
        left.init(left_seed, get_mbb(n, left_seed), get_labels(n, left_seed));
        right.init(right_seed, get_mbb(n, right_seed), get_labels(n, right_seed));

        // Holds the set of unassigned indices.
        boost::container::static_vector<u32, N> remaining;
        for (u32 i = 0; i < N; ++i) {
            if (i != left_seed && i != right_seed) {
                remaining.push_back(i);
            }
        }

        // Partition the remaining entries into left & right.
        // In each iteration, consider all remaining entries.
        // For each entry, select either the left or the right group,
        // depending on which cost value is better.
        // Then, assign the entry that had the highest difference in
        // cost values to its selected group.
        // Repeat until all entries have been assigned or the number of entries
        // for one of the groups has become too high.
        while (!remaining.empty()) {
            // Make sure that each partition will hold at least `min_entries` entries.
            // If one partition size reaches `limit`, all remaining entries will be
            // put into the other one.
            static constexpr size_t limit = max_entries - min_entries;
            auto add_rest = [&](Part& p) {
                for (u32 index : remaining) {
                    p.add(index, get_mbb(n, index), get_labels(n, index));
                }
                remaining.clear();
            };
            if (left.size() == limit) {
                add_rest(right);
                break;
            } else if (right.size() == limit) {
                add_rest(left);
                break;
            }

            // Pick a group for the given leaf element.
            // Pretend that we have an internal node at hand with only
            // 2 entries (the two partially constructed nodes).
            // Returns either the left or the right group, depending
            // on the cost value.
            // The second return value is the (absolute) difference in cost.
            // The higher the difference, the more meaningful the decision.
            auto pick_partition = [&](u32 index) {
                const auto mbb = get_mbb(n, index);
                const float norm = state.inverse(std::max(state.enlargement(left.get_mbb(), mbb),
                                                          state.enlargement(right.get_mbb(), mbb)));

                // Normalized and weighted cost of inserting the entry
                // into the given group.
                const auto cost = [&](const Part& p) {
                    const float spatial = state.spatial_cost(p.get_mbb(), mbb, norm);
                    const float textual = state.textual_cost(get_labels(n, index), get_total_units(n, index),
                                                             p.get_labels(), p.get_total_units());
                    return state.cost(spatial, textual);
                };

                const float lc = cost(left);
                const float rc = cost(right);
                return lc < rc ? std::make_tuple(&left, rc - lc) : std::make_tuple(&right, lc - rc);
            };

            // Determine the item with the best cost difference.
            u32* best_item = &remaining[0];
            Part* best_part = nullptr;
            float best_diff = 0;
            std::tie(best_part, best_diff) = pick_partition(remaining[0]);
            for (size_t i = 1; i < remaining.size(); ++i) {
                Part* part;
                float diff;
                std::tie(part, diff) = pick_partition(remaining[i]);

                if (diff > best_diff) {
                    best_item = &remaining[i];
                    best_part = part;
                    best_diff = diff;
                }
            }

            // Insert the item into its selected group and remove it from the vector.
            // Repeat until the vector is empty.
            best_part->add(*best_item, get_mbb(n, *best_item), get_labels(n, *best_item));
            std::swap(*best_item, remaining.back());
            remaining.pop_back();
        }

        geodb_assert(left.size() + right.size() == N, "all items must have been distributed");
    }

    /// Picks two distinct entry indices as seeds for two new nodes.
    /// The two entries selected will be the ones that would
    /// produce the hightest cost if they were put into the same node.
    template<typename OverflowingNode>
    std::tuple<u32, u32> pick_seeds(const OverflowingNode& n) const {
        static constexpr u32 N = OverflowingNode::N;

        // normalized wasted space and textual cost, combined using weight beta.
        const float norm = state.inverse(max_waste(n));
        auto cost = [&](u32 i, u32 j) {
            const float spatial = waste(get_mbb(n, i), get_mbb(n, j)) * norm;
            const float textual = state.textual_cost(get_labels(n, i), get_total_units(n, i),
                                                     get_labels(n, j), get_total_units(n, j));
            return state.cost(spatial, textual);
        };

        // Compute the pair that would produce the maximal cost
        // if its entries were to be placed into the same node.
        float max = -1.f;
        u32 left, right;
        left = right = std::numeric_limits<u32>::max();
        for (u32 i = 0; i < N; ++i) {
            for (u32 j = i + 1; j < N; ++j) {
                const float v = cost(i, j);
                if (v > max) {
                    max = v;
                    left = i;
                    right = j;
                }
            }
        }

        geodb_assert(left < N && right < N && left != right, "invalid seed incides");
        return std::make_tuple(left, right);
    }

private:
    /// Returns the amount of wasted space should the entries
    /// represented by `a` and `b` be stored in the same node.
    /// This is used by the node splitting algorithm to find the seeds
    /// for the old and the newly created node.
    float waste(const bounding_box& a, const bounding_box& b) const {
        const auto mbb = a.extend(b);
        return std::max(mbb.size() - a.size() - b.size(), 0.f);
    }

    /// Returns the maximal waste value for all bounding box combinations.
    /// Used for normalization of the individual values.
    template<typename Node>
    float max_waste(const Node& n) const {
        const u32 count = get_count(n);
        geodb_assert(count >= 2, "must have at least 2 entries");

        float max = std::numeric_limits<float>::lowest();
        for (u32 i = 0; i < count; ++i) {
            for (u32 j = i + 1; j < count; ++j) {
                max = std::max(max, waste(get_mbb(n, i), get_mbb(n, j)));
            }
        }
        return max;
    }

};

} // namespace geodb

#endif // IRWI_TREE_PARTITION_HPP
