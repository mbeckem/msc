#ifndef IRWI_TREE_STATE_HPP
#define IRWI_TREE_STATE_HPP

#include "geodb/bounding_box.hpp"
#include "geodb/common.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/utility/movable_adapter.hpp"

#include <boost/range/functions.hpp>
#include <boost/optional.hpp>

#include <limits>

namespace geodb {

/// A tree_state contains the basic state of a tree, including its storage.
/// It does not implement complex insert, delete or query operations.
///
/// \tparam StorageSpec
///     The type of storage used by this tree.
/// \tparam Value
///     The type stored in the tree leaves.
/// \tparam Accessor
///     The type used to access a leaf values id, bounding box and its labels.
/// \tparam Lambda
///     The maximum number of intervals in each posting.
///     Intervals are used to represent the set of trajectory ids.
template<typename StorageSpec, typename Value, typename Accessor, u32 Lambda>
class tree_state {
public:
    using storage_type = typename StorageSpec::template implementation<Value, Lambda>;

    using value_type = Value;

    using index_type = typename storage_type::index_type;
    using index_ptr = typename storage_type::index_ptr;
    using const_index_ptr = typename storage_type::const_index_ptr;

    using list_type = typename index_type::list_type;
    using list_ptr = typename index_type::list_ptr;
    using const_list_ptr = typename index_type::const_list_ptr;

    using posting_type = typename list_type::posting_type;

    using id_set_type = typename posting_type::trajectory_id_set_type;
    using dynamic_id_set_type = interval_set<trajectory_id_type>;

    using node_id_type = typename storage_type::node_id_type;
    using node_ptr = typename storage_type::node_ptr;
    using leaf_ptr = typename storage_type::leaf_ptr;
    using internal_ptr = typename storage_type::internal_ptr;

    static constexpr u32 lambda() { return Lambda; }

    /// Maximum fanout for internal nodes.
    static constexpr u32 max_internal_entries() {
        return storage_type::max_internal_entries();
    }

    /// Minimum number of entries in a internal node.
    /// Only used by the split algorithm because deletion
    /// is not (yet) supported.
    static constexpr u32 min_internal_entries() {
        return (max_internal_entries() + 2) / 3;
    }

    /// Maximum fanout for leaf nodes.
    static constexpr u32 max_leaf_entries() {
        return storage_type::max_leaf_entries();
    }

    /// Minimal number of entries in a leaf node (except for the root).
    /// Currently only used by the split algorithm because the tree
    /// does not support delete opertions.
    static constexpr u32 min_leaf_entries() {
        return (max_leaf_entries() + 2) / 3;
    }

private:
    /// The storage implements the manipulation of tree nodes.
    movable_adapter<storage_type> m_storage;

    /// Implements access to required leaf entry properties.
    Accessor m_accessor;

    /// A factor in [0, 1] for computing the weighted average
    /// between spatial and textual insertion cost.
    float m_weight = 0;

public:
    tree_state(StorageSpec s, Accessor accessor, float weight)
        : m_storage(in_place_t(), std::move(s))
        , m_accessor(std::move(accessor))
        , m_weight(weight)
    {
        if (m_weight < 0 || m_weight > 1.f)
            throw std::invalid_argument("weight must be in [0, 1]");
    }

    storage_type& storage() {
        return *m_storage;
    }

    const storage_type& storage() const {
        return *m_storage;
    }

    float weight() const {
        return m_weight;
    }

    /// Returns the trajectory the given entry belongs to.
    trajectory_id_type get_id(const value_type& v) const {
        return m_accessor.get_id(v);
    }

    /// Returns the minimal bounding box of the given leaf value.
    bounding_box get_mbb(const value_type& v) const {
        return m_accessor.get_mbb(v);
    }

    /// Returns a range of unspecified type that contains
    /// objects with the properties <label, count>.
    /// The sum of the counts must equal `get_total_count(v)`.
    /// The range must never be empty.
    auto get_label_counts(const value_type& v) const {
        auto result = m_accessor.get_label_counts(v);
        geodb_assert(!boost::empty(result), "label-count range is empty");
        return m_accessor.get_label_counts(v);
    }

    /// Returns the number of items in this leaf value.
    /// This value is always >= 1.
    /// Normal trees have trajectory units as leaf entries and will
    /// never have more than 1 item per value.
    /// However, some trees represent entire subtrees as leaf value
    /// in another tree and thus can contain more than one unit.
    u64 get_total_count(const value_type& v) const{
        u64 result = m_accessor.get_total_count(v);
        geodb_assert(result > 0, "total count is empty");
        return m_accessor.get_total_count(v);
    }

    /// Returns the bounding box for an entry of an internal node.
    bounding_box get_mbb(internal_ptr n, u32 i) const {
        return storage().get_mbb(n, i);
    }

    /// Returns the bounding box for an entry of a leaf node.
    bounding_box get_mbb(leaf_ptr n, u32 i) const {
        return get_mbb(storage().get_data(n, i));
    }

    /// Returns the bounding box that contains all entries
    /// of the given node.
    template<typename NodePointer>
    bounding_box get_mbb(NodePointer n) const {
        const u32 count = storage().get_count(n);
        geodb_assert(count > 0, "Empty node");

        bounding_box b = get_mbb(n, 0);
        for (u32 i = 1; i < count; ++i) {
            b = b.extend(get_mbb(n, i));
        }
        return b;
    }

    /// Returns the index of `child` in its parent.
    /// It is a programming error if the child does not exist in the parent.
    u32 index_of(internal_ptr parent, node_ptr child) const {
        if (auto i = optional_index_of(parent, child)) {
            return *i;
        }
        unreachable("child not found");
    }

    /// Returns the index of `child` in its parent.
    boost::optional<u32> optional_index_of(internal_ptr parent, node_ptr child) const {
        const u32 count = storage().get_count(parent);
        for (u32 i = 0; i < count; ++i) {
            if (storage().get_child(parent, i) == child) {
                return i;
            }
        }
        return {};
    }

    /// Returns the enlargement that would occurr for the bounding box `e`
    /// should the object represented by `b` be inserted.
    float enlargement(const bounding_box& e, const bounding_box& b) const {
        return e.extend(b).size() - e.size();
    }

    /// Inspects all child entries of the internal node
    /// and measures the enlargement that occurrs
    /// should `b` be inserted into the child's subtree.
    /// Returns the maximum of these values.
    float max_enlargement(internal_ptr n, const bounding_box& b) const {
        const u32 count = storage().get_count(n);
        geodb_assert(count > 0, "empty node");

        float max = enlargement(get_mbb(n, 0), b);
        for (u32 i = 1; i < count; ++i) {
            max = std::max(max, enlargement(get_mbb(n, i), b));
        }

        geodb_assert(max >= 0, "invalid enlargement value");
        return max;
    }

    /// Returns the spatial cost of inserting the new bounding box `b` into an existing
    /// subtree with bounding box `mbb`.
    /// Uses `norm` for normalization.
    float spatial_cost(const bounding_box& mbb, const bounding_box& b, float norm) {
        return enlargement(mbb, b) * norm;
    }

    /// Returns the textual cost for inserting a label into an existing subtree.
    ///
    /// \param unit_count
    ///     The number of units in that subtree that have that label.
    /// \param total_count
    ///     The total number of units in that subtree. Must not be zero.
    float textual_cost(u64 unit_count, u64 total_count) const {
        geodb_assert(total_count > 0, "there can be no empty subtrees");
        return 1.0f - float(unit_count) / float(total_count);
    }

    /// Generalized textual cost function.
    /// Computes the cost of merging two entire subtrees.
    ///
    /// \param labels1
    ///     A sorted range of <label, count> objects that contains
    ///     an entry for every label in the first subtree.
    /// \param total_count1
    ///     The total number of units in that subtree.
    /// \param labels2
    ///     A sorted range of <label, count> objects that contain
    ///     an entry for every label in the second subtree.
    /// \param total_count2
    ///     The total number of units in that subtree.
    template<typename LabelCount1, typename LabelCount2>
    float textual_cost(const LabelCount1& labels1, u64 total_count1,
                       const LabelCount2& labels2, u64 total_count2) const
    {
        const float total = total_count1 + total_count2;

        float max = 0;
        shared_labels(labels1, labels2, [&](label_type label, u64 count1, u64 count2) {
            unused(label);
            max = std::max(max, float(count1 + count2) / total);
        });
        return 1.0f - max;
    }

    /// Iterate over two sequences of <label, count> entries. The ranges must be sorted.
    /// The callback will be invoked for every pair of elements with the same label.
    template<typename LabelMap1, typename LabelMap2, typename Callback>
    void shared_labels(const LabelMap1& map1, const LabelMap2& map2, Callback&& cb) const {
        auto iter1 = map1.begin(), end1 = map1.end();
        auto iter2 = map2.begin(), end2 = map2.end();

        while (iter1 != end1 && iter2 != end2) {
            const auto& l1 = *iter1;
            const auto& l2 = *iter2;
            if (l1.label < l2.label) {
                ++iter1;
                geodb_assert(iter1 == end1 || iter1->label > l1.label, "sorted");
            } else if (l1.label > l2.label) {
                ++iter2;
                geodb_assert(iter2 == end2 || iter2->label > l2.label, "sorted");
            } else {
                cb(l1.label, l1.count, l2.count);
                ++iter1;
                ++iter2;
                geodb_assert(iter1 == end1 || iter1->label > l1.label, "sorted");
                geodb_assert(iter2 == end2 || iter2->label > l2.label, "sorted");
            }
        }
    }

    /// Returns the weighted average of the two values.
    float cost(float spatial, float textual) const {
        return m_weight * spatial + (1.0f - m_weight) * textual;
    }

    /// Returns the inverse of `value` or 0 if the value is very close to zero.
    /// This is used when 1.0 / value is intended as a normalization factor.
    static float inverse(float value) {
        static constexpr float min = std::numeric_limits<float>::epsilon() / 2.0f;
        if (value <= min)
            return 0;
        return 1.0f / value;
    }
};

} // namespace geodb

#endif // IRWI_TREE_STATE_HPP
