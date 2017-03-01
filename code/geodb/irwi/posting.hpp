#ifndef GEODB_IRWI_POSTING_HPP
#define GEODB_IRWI_POSTING_HPP

#include "geodb/common.hpp"
#include "geodb/interval_set.hpp"
#include "geodb/trajectory.hpp"

namespace geodb {

/// Identifies child entries of an internal node (by index).
using entry_id_type = u32;

template<u32 Lambda>
using trajectory_id_set = static_interval_set<trajectory_id_type, Lambda>;

/// Represents the data part of a postings list entry, i.e.
/// a count and a set of trajectory ids.
template<u32 Lambda>
class posting_data {
public:
    using trajectory_id_set_type = trajectory_id_set<Lambda>;

    using interval_type = typename trajectory_id_set_type::interval_type;

public:
    /// Creates an empty instance (with a count of zero and an empty id set).
    posting_data() = default;

    posting_data(u64 count, const trajectory_id_set_type& set) {
        this->count(count);
        this->id_set(set);
    }

    /// The number of trajectory units counted by this posting.
    u64 count() const { return m_count; }

    /// Updates the count of this instance.
    void count(u64 count) {
        m_count = count;
    }

    /// A set that contains (some superset of) the trajectory ids
    /// counted by this instance.
    /// This set can be used to quickly exclude some node froms being searched,
    /// e.g. if the associated subtree contains none of the required trajectory ids.
    trajectory_id_set_type id_set() const {
        return trajectory_id_set_type(m_intervals, m_intervals + m_intervals_count);
    }

    /// Updates the id_set of this instance.
    void id_set(const trajectory_id_set_type& set) {
        geodb_assert(set.size() <= Lambda, "size invariant");
        m_intervals_count = set.size();
        std::copy(set.begin(), set.end(), m_intervals);
    }

    friend bool operator==(const posting_data& a, const posting_data& b) {
        return a.m_count == b.m_count
                && a.m_intervals_count == b.m_intervals_count
                && std::equal(a.m_intervals, a.m_intervals + a.m_intervals_count, b.m_intervals);
    }

    friend bool operator!=(const posting_data& a, const posting_data& b) {
        return !(a == b);
    }

private:
    u64 m_count = 0;                    ///< The number of units with that label in this subtree.
    u32 m_intervals_count = 0;          ///< Number of intervals in the set.
    interval_type m_intervals[Lambda];  ///< Intervals representing the id set.
};

/// Every posting `p` belongs to a postings list `pl` which in
/// turn belongs to some internal node and a label `l`.
/// The existance of `p` implies that within the subtree of `p.node()`
/// there are `p.count()` trajectory units with the label `l`.
template<u32 Lambda>
class posting : public posting_data<Lambda> {
    using data_t = typename posting::posting_data;

public:
    using trajectory_id_set_type = trajectory_id_set<Lambda>;

    using interval_type = typename trajectory_id_set_type::interval_type;

public:
    posting(entry_id_type node, u64 count, const trajectory_id_set_type& ids)
        : m_node(node)
    {
        data_t::count(count);
        data_t::id_set(ids);
    }

    posting(entry_id_type node, posting_data<Lambda> data)
        : data_t(std::move(data))
        , m_node(node)
    {}

    /// The index of the associated node within the internal node
    /// which contains this posting.
    entry_id_type node() const { return m_node; }

    friend std::ostream& operator<<(std::ostream& o, const posting& e) {
        return o << "{node: " << e.node() << ", count: " << e.count() << ", ids: " << e.id_set() << "}";
    }

    friend bool operator==(const posting& a, const posting& b) {
        return a.m_node == b.m_node && static_cast<const data_t&>(a) == static_cast<const data_t&>(b);
    }

    friend bool operator!=(const posting& a, const posting& b) {
        return !(a == b);
    }

private:
    entry_id_type m_node = 0;   ///< Index of the entry in its internal node.
};

} // namespace geodb

#endif // GEODB_IRWI_POSTING_HPP
