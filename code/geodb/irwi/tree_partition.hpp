#ifndef GEODB_IRWI_TREE_PARTITION_HPP
#define GEODB_IRWI_TREE_PARTITION_HPP

#include "geodb/common.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/label_count.hpp"
#include "geodb/irwi/posting.hpp"
#include "geodb/utility/range_utils.hpp"

#include <boost/container/static_vector.hpp>

#include <array>
#include <map>
#include <unordered_set>

/// \file
/// The node splitting algorithm used by the IRWI Tree (when inserting
/// elements one-by-one).

namespace geodb {

/// Implements the generic node splitting algorithm for IRWI trees.
template<typename State>
class tree_partition {
    using storage_type = typename State::storage_type;

    using value_type = typename State::value_type;

    using id_set_type = typename State::id_set_type;

    using node_ptr = typename State::node_ptr;
    using leaf_ptr = typename State::leaf_ptr;
    using internal_ptr = typename State::internal_ptr;

    using posting_type = typename State::posting_type;

    template<typename Key, typename Value>
    using map_type = typename storage_type::template map_type<Key, Value>;

    template<typename Value>
    using buffer_type = typename storage_type::template buffer_type<Value>;

public:
    /// Identifies to which node an entry has been assigned.
    enum which_t {
        left, right
    };

    /// Maps an old index to a new index within one of the nodes.
    struct split_element {
        u32 old_index;  ///< Index into the old node.
        u32 new_index;  ///< Index into the new node.
        which_t which;  ///< Identifies the new node.

        split_element(u32 old_index, u32 new_index, which_t which)
            : old_index(old_index), new_index(new_index), which(which)
        {}
    };

    /// Represents the subtree of an entry of an internal node.
    struct internal_entry {
        /// Points to the child node.
        node_ptr ptr{};

        /// The subtrees bounding box.
        bounding_box mbb;

        /// Total number of units in this subtree.
        u64 total = 0;

        /// Sorted by label.
        buffer_type<label_count> labels;

        internal_entry(storage_type& storage)
            : labels(storage.template make_buffer<label_count>())
        {}
    };

private:
    /// Represents a leaf node that is currently overflowing.
    struct leaf_entries {
        const std::vector<value_type>& entries;
    };

    /// Represents an internal node that is currently overflowing.
    struct internal_entries {
        const std::vector<internal_entry>& entries;
    };

private:
    State& state;
    storage_type& storage;

public:
    tree_partition(State& state)
        : state(state)
        , storage(state.storage())
    {}


    /// Partitions the entries of a single node into two groups.
    /// Implements the quadradic node splitting algorithm for R-Trees
    /// (extended for spatio-textual trajectories).
    ///
    /// \param entries
    ///     The entries of the overflowing internal node.
    /// \param min_elements
    ///     The minimum number of elements for both parts.
    /// \param split
    ///     The vector that will contain the partition.
    void partition(const std::vector<internal_entry>& entries, size_t min_elements, std::vector<split_element>& split)
    {
        partition_impl(internal_entries{entries}, min_elements, split);
    }

    /// Partitions the entries of a single node into two groups.
    /// Implements the quadradic node splitting algorithm for R-Trees
    /// (extended for spatio-textual trajectories).
    ///
    /// \param entries
    ///     The entries of the overflowing leaf node.
    /// \param min_elements
    ///     The minimum number of elements for both parts.
    /// \param split
    ///     The vector that will contain the partition.
    void partition(const std::vector<value_type>& entries, size_t min_elements, std::vector<split_element>& split)
    {
        partition_impl(leaf_entries{entries}, min_elements, split);
    }

private:
    template<typename Entries>
    void partition_impl(const Entries& e, size_t min_elements, std::vector<split_element>& split)
    {
        const u32 N = get_count(e);
        const u32 limit = N - min_elements;

        geodb_assert(N >= 2 * min_elements, "Not enough items for min_elements constraint");

        split.clear();
        split.reserve(N);

        u32 left_seed, right_seed;
        std::tie(left_seed, right_seed) = pick_seeds(e);

        node_part left_part(storage, left, get_mbb(e, left_seed), get_labels(e, left_seed));
        node_part right_part(storage, right, get_mbb(e, right_seed), get_labels(e, right_seed));

        split.push_back(split_element(left_seed, 0, left));
        split.push_back(split_element(right_seed, 0, right));

        // Holds the set of unassigned indices.
        std::vector<u32> remaining;
        remaining.reserve(N - 2);
        for (u32 i = 0; i < N; ++i) {
            if (i != left_seed && i != right_seed) {
                remaining.push_back(i);
            }
        }

        // Add the index the the given part.
        auto add_index = [&](node_part& p, u32 index) {
            u32 new_index = p.add(get_mbb(e, index), get_labels(e, index));
            split.push_back(split_element(index, new_index, p.which()));
        };

        // Add all remaining elements into the given part.
        auto add_remaining = [&](node_part& p) {
            for (u32 index : remaining) {
                add_index(p, index);
            }
        };

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
            if (left_part.size() == limit) {
                add_remaining(right_part);
                break;
            } else if (right_part.size() == limit) {
                add_remaining(left_part);
                break;
            }

            // Pick a part for the given leaf element.
            // Pretend that we have an internal node at hand with only
            // 2 entries (the two partially constructed nodes).
            // Returns either the left or the right group, depending
            // on the cost value.
            // The second return value is the (absolute) difference in cost.
            // The higher the difference, the more meaningful the decision.
            auto pick_part = [&](u32 index) {
                const auto mbb = get_mbb(e, index);
                const float norm = state.inverse(std::max(state.enlargement(left_part.get_mbb(), mbb),
                                                          state.enlargement(right_part.get_mbb(), mbb)));

                // Normalized and weighted cost of inserting the entry
                // into the given group.
                const auto cost = [&](const node_part& p) {
                    const float spatial = state.spatial_cost(p.get_mbb(), mbb, norm);
                    const float textual = state.textual_cost(get_labels(e, index), get_total_units(e, index),
                                                             p.get_labels(), p.get_total_units());
                    return state.cost(spatial, textual);
                };

                const float lc = cost(left_part);
                const float rc = cost(right_part);
                return lc < rc ? std::make_tuple(&left_part, rc - lc) : std::make_tuple(&right_part, lc - rc);
            };

            // Determine the item with the best cost difference.
            u32* best_item = &remaining[0];
            node_part* best_part = nullptr;
            float best_diff = 0;
            std::tie(best_part, best_diff) = pick_part(remaining[0]);
            for (u32 i = 1; i < remaining.size(); ++i) {
                node_part* part;
                float diff;
                std::tie(part, diff) = pick_part(remaining[i]);

                if (diff > best_diff) {
                    best_item = &remaining[i];
                    best_part = part;
                    best_diff = diff;
                }
            }

            // Insert the item into its selected group and remove it from the vector.
            // Repeat until the vector is empty.
            add_index(*best_part, *best_item);
            std::swap(*best_item, remaining.back());
            remaining.pop_back();
        }
    }

private:
    bounding_box get_mbb(const leaf_entries& n, u32 index) const {
        geodb_assert(index < n.entries.size(), "index out of bounds");
        return state.get_mbb(n.entries[index]);
    }

    u32 get_count(const leaf_entries& n) const {
        return n.entries.size();
    }

    auto get_labels(const leaf_entries& n, u32 index) const {
        geodb_assert(index < n.entries.size(), "index out of bounds");
        return state.get_label_counts(n.entries[index]);
    }

    u64 get_total_units(const leaf_entries& n, u32 index) const {
        geodb_assert(index < n.entries.size(), "index out of bounds");
        return state.get_total_count(n.entries[index]);
    }

    bounding_box get_mbb(const internal_entries& o, u32 index) const {
        return o.entries[index].mbb;
    }

    u64 get_total_units(const internal_entries& o, u32 index) const {
        return o.entries[index].total;
    }

    auto get_labels(const internal_entries& o, u32 index) const {
        auto& labels = o.entries[index].labels;
        return boost::make_iterator_range(labels.begin(), labels.end());
    }

    u32 get_count(const internal_entries& o) const {
        return o.entries.size();
    }

    class node_part : boost::noncopyable {
    private:
        /// This is the left or right group.
        which_t m_which;

        /// Minimal bounding box for all entries.
        bounding_box m_mbb;

        /// Map of label -> count.
        map_type<label_type, u64> m_labels;

        /// Number of entries.
        u32 m_size = 0;

        /// Total number of units in this subtree.
        u64 m_total_units = 0;

    public:
        template<typename LabelCounts>
        node_part(storage_type& storage, which_t which, const bounding_box& seed_mbb, const LabelCounts& seed_labels)
            : m_which(which)
            , m_mbb(seed_mbb)
            , m_labels(storage.template make_map<label_type, u64>())
            , m_size(1)
            , m_total_units(0)
        {
            for (const auto& lc : seed_labels) {
                m_labels.insert(lc.label, lc.count);
                m_total_units += lc.count;
            }
        }

        which_t which() const {
            return m_which;
        }

        const bounding_box& get_mbb() const {
            return m_mbb;
        }

        auto get_labels() const {
            auto transform = [](const auto& pair) {
                return label_count{pair.first, pair.second};
            };
            return m_labels | transformed(transform);
        }

        u64 get_total_units() const {
            return m_total_units;
        }

        u32 size() const {
            return m_size;
        }

        /// Adds the new entry and returns its index in this part.
        template<typename LabelCounts>
        u32 add(const bounding_box& b, const LabelCounts& label_counts) {
            m_mbb = m_mbb.extend(b);

            const auto end = m_labels.end();
            for (const auto& lc : label_counts) {
                auto pos = m_labels.find(lc.label);
                if (pos != end) {
                    m_labels.replace(pos, pos->second + lc.count);
                } else {
                    m_labels.insert(lc.label, lc.count);
                }

                m_total_units += lc.count;
            }
            return m_size++;
        }
    };

    /// Picks two distinct entry indices as seeds for two new nodes.
    /// The two entries selected will be the ones that would
    /// produce the hightest cost if they were put into the same node.
    template<typename Entries>
    std::tuple<u32, u32> pick_seeds(const Entries& e) const {
        const u32 N = get_count(e);

        // normalized wasted space and textual cost, combined using weight beta.
        const float norm = state.inverse(max_waste(e));
        auto cost = [&](u32 i, u32 j) {
            const float spatial = waste(get_mbb(e, i), get_mbb(e, j)) * norm;
            const float textual = state.textual_cost(get_labels(e, i), get_total_units(e, i),
                                                     get_labels(e, j), get_total_units(e, j));
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
    template<typename Entries>
    float max_waste(const Entries& n) const {
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

#endif // GEODB_IRWI_TREE_PARTITION_HPP
