#ifndef IRWI_POSTINGS_LIST_HPP
#define IRWI_POSTINGS_LIST_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/posting.hpp"
#include "geodb/utility/movable_adapter.hpp"

#include <boost/iterator/iterator_facade.hpp>
#include <tpie/serialization2.h>

#include <vector>

namespace geodb {

template<typename PostingsList>
class postings_list_iterator : public boost::iterator_facade<
    postings_list_iterator<PostingsList>,   // Self
    typename PostingsList::posting_type,    // Value
    std::random_access_iterator_tag,        // Traversal
    typename PostingsList::posting_type,    // Reference (here: same as value)
    std::ptrdiff_t                          // Difference
>
{
    using postings_list_type = PostingsList;

    using storage_type = typename PostingsList::storage_type;

public:
    using posting_type = typename postings_list_type::posting_type;

public:
    postings_list_iterator() {}

private:
    friend class boost::iterator_core_access;

    const posting_type dereference() const {
        geodb_assert(m_index < storage().size(), "index out of bounds");
        return storage().get(m_index);
    }

    bool equal(const postings_list_iterator& other) const {
        geodb_assert(m_list == other.m_list, "iterators belong to different lists");
        return m_index == other.m_index;
    }

    void advance(std::ptrdiff_t n) {
        m_index = static_cast<std::ptrdiff_t>(m_index) + n;
    }

    void increment() {
        ++m_index;
    }

    void decrement() {
        --m_index;
    }

    std::ptrdiff_t distance_to(const postings_list_iterator& other) const {
        return static_cast<std::ptrdiff_t>(other.m_index) - static_cast<std::ptrdiff_t>(m_index);
    }

    const storage_type& storage() const {
        geodb_assert(m_list != nullptr, "invalid iterator");
        return m_list->storage();
    }

private:
    template<typename StorageSpec, u32 Lambda>
    friend class postings_list;

    postings_list_iterator(const postings_list_type* list, size_t index)
        : m_list(list)
        , m_index(index) {}

    const postings_list_type* m_list = nullptr;
    size_t m_index = 0;
};

/// A summary of the entries of a single postings_list.
template<u32 Lambda>
struct postings_list_summary {
    using trajectory_id_set_type = trajectory_id_set<Lambda>;

    u64 count = 0;
    trajectory_id_set_type trajectories;

    postings_list_summary() {}

    postings_list_summary(u64 count, trajectory_id_set_type trajectories)
        : count(count)
        , trajectories(std::move(trajectories))
    {}

    template<typename Dst>
    friend void serialize(Dst& dst, const postings_list_summary& sum) {
        using tpie::serialize;
        serialize(dst, sum.count);
        serialize(dst, sum.trajectories);
    }

    template<typename Src>
    friend void unserialize(Src& src, postings_list_summary& sum) {
        using tpie::unserialize;
        unserialize(src, sum.count);
        unserialize(src, sum.trajectories);
    }
};

/// A postings list contains index information about the children of an
/// internal node for a single label.
///
/// Every postings list entry corresponds to a single entry
/// of an internal node and contains a child node's identifier and
/// a summary for the current label. The size of the postings list
/// is therefore limited by the maximum number of entries in an internal node.
///
/// A postings list can be iterated using the random-access-iterators
/// returned by \ref begin() and \ref end().
template<typename StorageSpec, u32 Lambda>
class postings_list {
public:
    using iterator = postings_list_iterator<postings_list>;
    using const_iterator = iterator;

    using posting_type = posting<Lambda>;

    using summary_type = postings_list_summary<Lambda>;

private:
    template<typename PostingList>
    friend class postings_list_iterator;

    using storage_type = typename StorageSpec::template implementation<posting_type>;

public:
    postings_list(StorageSpec s = StorageSpec())
        : m_storage(in_place_t(), std::move(s)) {}

    iterator begin() const { return { this, 0 }; }

    iterator end() const { return { this, storage().size() }; }

    /// Iterates through the list to find an entry with the given id.
    iterator find(entry_id_type id) const {
        return std::find_if(begin(), end(), [&](const posting_type& e) {
            return e.node() == id;
        });
    }

    /// Insert the new entry at the position pointed to by `pos`.
    /// \pre `pos` is a valid iterator.
    void set(iterator pos, const posting_type& e) {
        geodb_assert(pos.m_list == this, "iterator must belong to this list");
        storage().set(pos.m_index, e);
    }

    /// Appends the new entry to the end of the list.
    void append(const posting_type& e) {
        storage().push_back(e);
    }

    /// Appends all entries in the range [begin, end).
    template<typename Iter>
    void append(Iter begin, Iter end) {
        for (; begin != end; ++begin) {
            append(*begin);
        }
    }

    /// Equivalent to \ref clear() followed by \ref append.
    template<typename Iter>
    void assign(Iter begin, Iter end) {
        clear();
        append(begin, end);
    }

    /// Reads all entries of this posting list into a vector.
    std::vector<posting_type> all() const {
        std::vector<posting_type> result;
        result.reserve(size());
        for (const posting_type& e : *this) {
            result.push_back(e);
        }
        return result;
    }

    /// Fetches the counters for every child entry of the parent node.
    /// Returns a vector where the count for child `i` is at index `i`.
    /// Because not every child entry may have an entry in this list,
    /// this function requires the total number of children as a parameter.
    ///
    /// \param total    The total number of child entries in the parent node.
    std::vector<u64> counts(entry_id_type total) const {
        std::vector<u64> result(total);
        for (const posting_type& e : *this) {
            geodb_assert(e.node() < total, "total not large enough");
            result[e.node()] = e.count();
        }
        return result;
    }

    /// Creates a summary of this list.
    /// The summary contains the total number of units and a set
    /// storing (an approximation of) the trajectory ids of those units.
    summary_type summarize() const {
        using id_set_t = typename posting_type::trajectory_id_set_type;

        summary_type result;
        std::vector<id_set_t> sets;
        sets.reserve(size());
        for (const posting_type& p : *this) {
            result.count += p.count();
            sets.push_back(p.id_set());
        }
        result.trajectories = id_set_t::set_union(sets);
        return result;
    }

    /// Removes all entries from this list.
    void clear() {
        storage().clear();
    }

    /// Removes the entry at the position pointed to by `pos`.
    /// The last element of the list (if any) will be swapped into this
    /// position to fill the gap.
    ///
    /// Invalidates all iterators to the last item of the list.
    ///
    /// \pre `pos` is a valid iterator.
    void remove(iterator pos) {
        geodb_assert(pos.m_list == this, "iterator must belong to this list");
        geodb_assert(pos.m_index < storage().size(),
                     "iterator does not point to a valid element");
        if (pos.m_index != storage().size() - 1) {
            storage().set(pos.m_index, storage().get(storage().size() - 1));
        }
        storage().pop_back();
    }

    /// Returns the entry at the given index.
    /// Note that this function returns a value and not a reference.
    posting_type operator[](size_t index) const {
        return storage().get(index);
    }

    /// Returns the number of entries in this list.
    size_t size() const {
        return storage().size();
    }

    /// Returns true if this list is empty.
    bool empty() const { return size() == 0; }

private:
    storage_type& storage() { return *m_storage; }
    const storage_type& storage() const { return *m_storage; }

private:
    movable_adapter<storage_type> m_storage;
};

} // namespace geodb

#endif // IRWI_POSTINGS_LIST_HPP
