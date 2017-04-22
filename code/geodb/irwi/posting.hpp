#ifndef GEODB_IRWI_POSTING_HPP
#define GEODB_IRWI_POSTING_HPP

#include "geodb/common.hpp"
#include "geodb/id_set.hpp"
#include "geodb/null_set.hpp"
#include "geodb/trajectory.hpp"

/// \file
/// Contains the definition of the `posting` class,
/// which is used the lists of an inverted index.

namespace geodb {

/// Identifies child entries of an internal node (by index).
using entry_id_type = u32;

template<u32 Lambda>
class posting_data;

/// A posting with Lambda == 0 does not store any ids.
template<>
class posting_data<0> {
public:
    using id_set_type = null_set<trajectory_id_type>;

public:
    posting_data() = default;

    posting_data(u64 count, const id_set_type&): m_count(count) {}

    /// The number of trajectory units counted by this posting.
    u64 count() const {
        return m_count;
    }

    /// Updates the count of this instance.
    void count(u64 count) {
        m_count = count;
    }

    void id_set(const id_set_type&) {}

    id_set_type id_set() const { return {}; }

    friend bool operator==(const posting_data& a, const posting_data& b) {
        return a.count() == b.count();
    }

    friend bool operator!=(const posting_data& a, const posting_data& b) {
        return !(a == b);
    }

    friend std::ostream& operator<<(std::ostream& o, const posting_data& p) {
        return o << "count: " << p.count() << ", ids: NONE";
    }

private:
    u64 m_count = 0;
};

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
    u64 count() const {
        return m_count;
    }

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
        return a.count() == b.count()
                && std::memcmp(&a.m_binary_ids, &b.m_binary_ids, sizeof(id_set_binary_type)) == 0;
    }

    friend bool operator!=(const posting_data& a, const posting_data& b) {
        return !(a == b);
    }

    friend std::ostream& operator<<(std::ostream& o, const posting_data& data) {
        return o << "count: " << data.count()
                 << ", ids: " << data.id_set();
    }

private:
    u64 m_count = 0;
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
    posting(): posting(0) {}

    posting(entry_id_type node)
        : data_type()
        , m_node(node)
    {}

    posting(entry_id_type node, u64 count, const id_set_type& ids)
        : data_type(count, ids)
        , m_node(node)
    {}

    posting(entry_id_type node, data_type data)
        : data_type(std::move(data))
        , m_node(node)
    {}

    /// The index of the associated node within the internal node
    /// which contains this posting.
    entry_id_type node() const { return m_node; }

    friend std::ostream& operator<<(std::ostream& o, const posting& e) {
        return o << "{node: " << e.node() << ", " << static_cast<const data_type&>(e) << "}";
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
