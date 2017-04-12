#ifndef GEODB_IRWI_POSTING_HPP
#define GEODB_IRWI_POSTING_HPP

#include "geodb/common.hpp"
#include "geodb/id_set.hpp"
#include "geodb/trajectory.hpp"

/// \file
/// Contains the definition of the \ref posting class,
/// which is used in \ref postings_list.

namespace geodb {

/// Identifies child entries of an internal node (by index).
using entry_id_type = u32;

/// Represents the data part of a postings list entry, i.e.
/// a count and a set of trajectory ids.
template<u32 Lambda>
class posting_data {
public:
    using id_set_type = geodb::id_set<Lambda>;

private:
    using id_set_binary_type = geodb::id_set_binary<Lambda>;

public:
    /// Creates an empty instance (with a count of zero and an empty id set).
    posting_data() = default;

    posting_data(u64 count, const id_set_type& set) {
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
    id_set_type id_set() const {
        id_set_type set;
        from_binary<Lambda>(set, m_binary_ids);
        return set;
    }

    /// Updates the id_set of this instance.
    void id_set(const id_set_type& set) {
        to_binary<Lambda>(set, m_binary_ids);
    }

    friend bool operator==(const posting_data& a, const posting_data& b) {
        return a.m_count == b.m_count
                && std::memcmp(&a.m_binary_ids, &b.m_binary_ids, sizeof(id_set_binary_type)) == 0;
    }

    friend bool operator!=(const posting_data& a, const posting_data& b) {
        return !(a == b);
    }

private:
    u64 m_count = 0;                    ///< The number of units with that label in this subtree.
    id_set_binary_type m_binary_ids;
};

/// Every posting `p` belongs to a postings list `pl` which in
/// turn belongs to some internal node and a label `l`.
/// The existance of `p` implies that within the subtree of `p.node()`
/// there are `p.count()` trajectory units with the label `l`.
template<u32 Lambda>
class posting : public posting_data<Lambda> {
public:
    using data_type = typename posting::posting_data;

    using typename posting::posting_data::id_set_type;

public:
    posting(entry_id_type node)
        : data_type()
        , m_node(node)
    {}

    posting(entry_id_type node, u64 count, const id_set_type& ids)
        : m_node(node)
    {
        data_type::count(count);
        data_type::id_set(ids);
    }

    posting(entry_id_type node, data_type data)
        : data_type(std::move(data))
        , m_node(node)
    {}

    /// The index of the associated node within the internal node
    /// which contains this posting.
    entry_id_type node() const { return m_node; }

    friend std::ostream& operator<<(std::ostream& o, const posting& e) {
        return o << "{node: " << e.node() << ", count: " << e.count() << ", ids: " << e.id_set() << "}";
    }

    friend bool operator==(const posting& a, const posting& b) {
        return a.m_node == b.m_node && static_cast<const data_type&>(a) == static_cast<const data_type&>(b);
    }

    friend bool operator!=(const posting& a, const posting& b) {
        return !(a == b);
    }

private:
    entry_id_type m_node = 0;   ///< Index of the entry in its internal node.
};

} // namespace geodb

#endif // GEODB_IRWI_POSTING_HPP
