#ifndef GEODB_IRWI_POSTINGS_LIST_HPP
#define GEODB_IRWI_POSTINGS_LIST_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/posting.hpp"
#include "geodb/utility/movable_adapter.hpp"

#include <boost/iterator/iterator_facade.hpp>
#include <tpie/serialization2.h>

#include <vector>

namespace geodb {

/// A summary of the entries of a single postings_list.
template<u32 Lambda>
struct postings_list_summary {
    using id_set_type = typename posting_data<Lambda>::id_set_type;

    u64 count = 0;
    id_set_type trajectories;

    postings_list_summary() {}

    postings_list_summary(u64 count, id_set_type trajectories)
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
    using posting_type = posting<Lambda>;

    using summary_type = postings_list_summary<Lambda>;

private:
    template<typename PostingList>
    friend class postings_list_iterator;

    using storage_type = typename StorageSpec::template implementation<posting_type>;

    using storage_iterator = typename storage_type::iterator;

public:
    class iterator : public boost::iterator_adaptor<
            // Derived, Base, Value, Traversal, Reference, Difference
            iterator, storage_iterator, posting_type,
            std::bidirectional_iterator_tag,
            posting_type
        >
    {
        using base_t = typename iterator::iterator_adaptor;

    private:
        const postings_list* m_list = nullptr;

    public:
        iterator(): base_t() {}

    private:
        friend class postings_list;

        iterator(const postings_list* list, storage_iterator pos)
            : base_t(std::move(pos))
            , m_list(list)
        {}
    };

    using const_iterator = iterator;


public:
    postings_list(const StorageSpec& s = StorageSpec())
        : m_storage(s.template construct<posting_type>()) {}

    iterator begin() const { return { this, storage().begin() }; }

    iterator end() const { return { this, storage().end() }; }

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
        storage().set(pos.base(), e);
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

    /// Creates a summary of this list.
    /// The summary contains the total number of units and a set
    /// storing (an approximation of) the trajectory ids of those units.
    summary_type summarize() const {
        using id_set_t = typename posting_type::id_set_type;

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
        geodb_assert(pos != end(), "iterator does not point to a valid element");
        geodb_assert(size() > 0, "list cannot be empty at this point");

        auto last = std::prev(end());
        if (pos != last) {
            storage().set(pos.base(), *last);
        }
        storage().pop_back();
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

#endif // GEODB_IRWI_POSTINGS_LIST_HPP
