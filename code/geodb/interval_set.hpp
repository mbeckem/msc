#ifndef GEODB_INTERVAL_SET_HPP
#define GEODB_INTERVAL_SET_HPP

#include "geodb/algorithm.hpp"
#include "geodb/common.hpp"
#include "geodb/interval.hpp"
#include "geodb/type_traits.hpp"

#include <boost/range/adaptor/indirected.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm/equal.hpp>
#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/concepts.hpp>
#include <boost/range/counting_range.hpp>
#include <boost/range/empty.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/range/sub_range.hpp>
#include <boost/range/metafunctions.hpp>
#include <boost/range/numeric.hpp>
#include <tpie/serialization2.h>

#include <algorithm>
#include <ostream>
#include <queue>
#include <vector>

namespace geodb {

namespace detail {
    /// Represents the start or end point of some interval.
    template<typename T>
    struct interval_event {
        enum kind_t {
            open = 1, close = 2
        };

        kind_t kind;
        T point;

        // Ordered by point. Open events come before close events on the same point.
        bool operator<(const interval_event& other) const {
            return point < other.point || (point == other.point && kind < other.kind);
        }

        bool operator==(const interval_event& other) const {
            return kind == other.kind && point == other.point;
        }
    };

    /// Takes a range of interval-ranges and invokes the provided callback
    /// for every interval event (open or close) in sorted ascernding order.
    /// Each individual interval range must be in sorted order (ascending)
    /// with no overlapping intervals.
    /// Intervals from different ranges may overlap.
    ///
    /// This function serves as a building block for plane-sweep algorithms over
    /// a series of interval sets.
    ///
    /// Runtime complexity: O(N log M) where N is the total number of intervals
    /// and M is the number of nested ranges.
    template<typename Range, typename Callback>
    void interval_events(const Range& rng, Callback&& cb) {
        using nested_range_type = typename boost::range_value<const Range>::type;

        using nested_iterator_type = typename boost::range_iterator<
            typename boost::range_value<const Range>::type
        >::type;

        using interval_type = typename boost::range_value<nested_range_type>::type;

        using point_type = typename interval_type::value_type;

        using event_type = interval_event<point_type>;

        static_assert(is_specialization_of<interval_type, interval>::value,
                      "nested ranges must contain intervals");
        static_assert(std::is_lvalue_reference<typename boost::range_reference<const Range>::type>::value,
                      "nested ranges must be lvalues (i.e. no temporaries).");

        BOOST_CONCEPT_ASSERT(( boost::ForwardRangeConcept<const Range> ));
        BOOST_CONCEPT_ASSERT(( boost::ForwardRangeConcept<const nested_range_type> ));

        // A cursor tracks the iterator position within one of the subranges.
        // Every interval is visited twice, once for the start and once for the end point.
        struct cursor_t {
        private:
            typename event_type::kind_t kind;
            nested_iterator_type pos;
            nested_iterator_type end;

        public:
            cursor_t(nested_iterator_type pos, nested_iterator_type end)
                : kind(event_type::open)
                , pos(pos), end(end)
            {}

            bool at_end() const { return pos == end; }

            event_type event() const {
                geodb_assert(pos != end, "not at end");
                return kind == event_type::open
                          ? event_type{kind, pos->begin()}
                          : event_type{kind, pos->end()};
            }

            void advance() {
                geodb_assert(pos != end, "not at end");
                if (kind == event_type::open) {
                    kind = event_type::close;
                    return;
                }

                ++pos;
                kind = event_type::open;
            }

            bool operator<(const cursor_t& other) const { return event() < other.event(); }
            bool operator>(const cursor_t& other) const { return other < *this; }
        };

        // A min heap that keeps track of the closest cursor.
        std::priority_queue<cursor_t, std::vector<cursor_t>, std::greater<cursor_t>> queue;
        for (auto&& nested_range : rng) {
            cursor_t c(boost::begin(nested_range), boost::end(nested_range));
            if (!c.at_end()) {
                queue.push(std::move(c));
            }
        }

        while (!queue.empty()) {
            // Pop the closest cursor and get one event from it. Reinsert the
            // cursor if there are more elements.
            cursor_t c = std::move(queue.top());
            queue.pop();

            cb(c.event());

            c.advance();
            if (!c.at_end()) {
                queue.push(std::move(c));
            }
        }
    }

    /// Takes a vector of intervals and a range of iterators into that vector.
    /// Every element pointed to by an interator in `remove` will be merged with its
    /// immediate successor.
    /// \pre `remove` must be sorted.
    /// \warning the last interval does *not* have a successor.
    template<typename T, typename IteratorRange>
    void merge_positions(std::vector<interval<T>>& intervals, const IteratorRange& remove) {
        using iterator = typename std::vector<interval<T>>::iterator;

        auto remove_pos = boost::begin(remove); // points to next iterator to be removed
        auto remove_end = boost::end(remove);

        iterator out = intervals.begin();   // position in output range
        iterator in = out;                  // position in input range
        iterator last = intervals.end();
        while (remove_pos != remove_end) {
            if (in == *remove_pos) {
                // This interval should be removed and merged with its successor.
                // Simply skip this interval when copying and update the begin value
                // of the next interval.
                T begin = in->begin();
                ++in;
                *in = interval<T>(begin, in->end());
                ++remove_pos;
            } else {
                // Keep this interval by copying it into the output range.
                *out = *in;
                ++out;
                ++in;
            };
        }
        // End of "remove" has been reached; keep all remaining intervals.
        for (; in != last; ++in, ++out) {
            *out = *in;
        }
        intervals.erase(out, intervals.end());
    }

    /// Merges adjacent intervals until there are no more than
    /// `capacity` intervals in total. Chooses the intervals with the
    /// smallest gaps in between.
    /// The interval vector is modified in-place.
    template<typename T>
    void merge_intervals(std::vector<interval<T>>& intervals, size_t capacity) {
        using iterator = typename std::vector<interval<T>>::iterator;

        geodb_assert(capacity > 0, "invalid capacity");

        const size_t size = intervals.size();
        if (size <= capacity) {
            return;
        }

        // Compute the k smallest gaps.
        // After closing the gaps, the size of the interval vector will
        // be equal to the caspacity.
        // An iterator represents the interval it points to and its successor.
        const size_t k = size - capacity;
        std::vector<iterator> gaps(k);
        k_smallest(boost::counting_range(intervals.begin(), intervals.end() - 1), k, gaps,
            // Compare the distances between the interval pairs.
            [](const iterator& pos1, const iterator& pos2) {
                geodb_assert(pos1[1].begin() > pos1[0].end(), "intervals must be ordered");
                geodb_assert(pos2[1].begin() > pos2[0].end(), "intervals must be ordered");
                return pos1[1].begin() - pos1[0].end() < pos2[1].begin() - pos2[0].end();
            }
        );

        // Intervals are ordered by merge cost, now sort them in positional order.
        std::sort(gaps.begin(), gaps.end());

        // Close the gaps in place.
        return merge_positions(intervals, gaps);
    }

    /// Takes an arbitrary range of intervals and returns their union in sorted order.
    /// In other words, overlapping intervals are merged and intervals completely
    /// contained in others are removed.
    // TODO: Unneeded?
//    template<typename Range>
//    std::vector<interval> merge_overlapping(const Range& rng) {
//        // Traverse intervals in sorted order (sorted by begin, ascending).
//        std::vector<interval> intervals(boost::begin(rng), boost::end(rng));
//        std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b) {
//            return a.begin() < b.begin();
//        });

//        std::vector<interval> output;
//        if (intervals.empty()) {
//            return output;
//        }

//        // Keep track of the current active interval, which is the last interval
//        // in the output vector.
//        // If the next interval overlaps, grow the active interval.
//        // Otherwise, add a new interval to the output vector.
//        auto iter = intervals.begin();
//        output.push_back(*iter++);
//        for (const interval& i : boost::make_iterator_range(iter, intervals.end())) {
//            interval& active = output.back();

//            if (i.begin() <= active.end()) {
//                active = interval(active.begin(), std::max(active.end(), i.end()));
//            } else {
//                output.push_back(i);
//            }
//        }
//        return output;
//    }

} // namespace detail

/// Represents a set of integers as intervals.
/// Points can be inserted as intervals of size 1.
/// Using \ref trim(), one can reduce the size of the set
/// by merging neighboring intervals, zhus introducing some error.
///
/// Efficient algorithms for computing the union / intersection of an
/// arbitrary number of interval_sets are provided.
///
/// Note: This class is backed by a sorted vector. Insert performance could
/// bed improved by using a balanced search tree instead.
/// Intervals are sorted and do not overlap, thus a "normal" 1-D tree
/// is sufficient.
template<typename T>
class interval_set {
    using storage_type = std::vector<interval<T>>;

public:
    using iterator = typename storage_type::const_iterator;
    using const_iterator = iterator;

    using interval_type = interval<T>;
    using point_type = T;

public:
    /// Takes a range of interval_sets and computes their union.
    ///
    /// Runtime complexity: O(N log M) where N is the total number of intervals across all ranges
    /// and M is the number of ranges.
    template<typename Range>
    static interval_set set_union(Range&& rng)
    {
        using detail::interval_events;

        // Sweep over the plane and keep track of open intervals.
        // Whenever the open counter reaches zero, an element of the union has been found.
        std::vector<interval_type> result;
        point_type begin;   // Start of the earliest still active interval.
        size_t open = 0;    // Number of open intervals.
        interval_events(rng, [&](const auto& event) {
            if (event.kind == event.open) {
                if (++open == 1) {
                    begin = event.point;
                }
            } else {
                if (open-- == 1) {
                    result.push_back(interval_type(begin, event.point));
                }
            }
        });
        return interval_set(std::move(result));
    }

    /// Takes a range of interval_sets and return their intersection.
    ///
    /// Runtime complexity: O(N log M) where N is the total number of intervals across all ranges
    /// and M is the number of ranges.
    template<typename Range>
    static interval_set set_intersection(Range&& rng) {
        using detail::interval_events;

        // Sweep over the plane and keep track of open intervals.
        // When a close event is encountered and the count of open intervals
        // equals the number of sets in `rng`, then the closing interval
        // is a member of the intersection.
        // Note: Intervals in individual sets do not overlap, thus the count
        // of open intervals can never be greater than `size`.
        std::vector<interval_type> result;
        size_t size = boost::size(rng);
        point_type begin = 0;   // Start of the intersection interval.
        size_t open = 0;        // Number of open intervals.
        interval_events(rng, [&](const auto& event) {
            geodb_assert(open <= size, "too many active intervals");

            if (event.kind == event.open) {
                if (++open == size) {
                    begin = event.point;
                }
            } else {
                if (open-- == size) {
                    result.push_back(interval_type(begin, event.point));
                }
            }
        });
        return interval_set(std::move(result));
    }

public:
    /// Creates an empty interval set.
    /// \post `size() == 0`.
    interval_set() {}

    /// Creates a new interval set from the given list of intervals.
    /// The list must be sorted (by start coordinate, ascending) and
    /// adjacent intervals must not overlap.
    interval_set(std::initializer_list<interval_type> list):
        interval_set(list.begin(), list.end()) {}

    /// \copydoc interval_set(std::initializer_list<interval_type>)
    template<typename FwdIterator>
    interval_set(FwdIterator first, FwdIterator last)
        : m_intervals(first, last)
    {
        assert_invariant();
    }

private:
    interval_set(storage_type&& vec): m_intervals(std::move(vec)) {}

public:
    /// Returns the iterator to the first element.
    /// The itervals are disjoint and sorted.
    iterator begin() const { return m_intervals.begin(); }

    /// Returns the past-the-end iterator.
    iterator end() const { return m_intervals.end(); }

    /// Returns the interval at the given index.
    /// \pre `index < size()`.
    const interval_type& operator[](size_t index) const {
        geodb_assert(index < size(), "index out of bounds");
        return *(begin() + index);
    }

    /// Returns true iff `size() == 0`.
    bool empty() const { return size() == 0; }

    /// Returns the size (the number of intervals) of this set.
    size_t size() const { return m_intervals.size(); }

    /// Assign a new set of intervals. Reuses existing capacity.
    template<typename FwdIterator>
    void assign(FwdIterator first, FwdIterator last) {
        m_intervals.assign(first, last);
        assert_invariant();
    }

    /// Adds a point to this set.
    ///
    ///     - If some interval already contains this point, nothing needs to be done.
    ///     - Otherwise, insert a new point-like interval at the appropriate position.
    ///
    /// Runtime complexity: O(size()).
    ///
    /// \post `contains(point)`.
    /// \return Returns true iff adding the point caused the set to change, i.e. when
    ///         the point was not already represented by one of the intervals.
    bool add(point_type point) {
        const auto begin = mut_begin();
        const auto end = mut_end();
        const auto pos = interval_before(point);
        geodb_assert(pos == end || point >= pos->begin(), "");

        if (pos != end && pos->contains(point)) {
            // Point already represented.
            return false;
        }

        geodb_assert(pos == end || point > pos->end(),
                     "point must lie to the right of the found interval");
        m_intervals.insert(pos != end ? pos + 1 : begin, interval_type(point));

        geodb_assert(contains(point), "postcondition violated");
        return true;
    }

    /// Returns true if this set contains the given point.
    /// Runtime complexity: O(log(size)).
    bool contains(point_type point) const {
        const auto pos = interval_before(point);
        if (pos == end()) {
            return false;
        }

        geodb_assert(pos->begin() <= point, "interval to the left");
        return pos->contains(point);
    }

    /// Trims this set to fit the new size.
    /// Excess intervals will be merged.
    /// \pre `size > 0`.
    /// \post `this->size() <= size`.
    void trim(size_t size) {
        detail::merge_intervals(m_intervals, size);
        geodb_assert(m_intervals.size() <= size, "postcondition failure.");
    }

    /// Resets this instance.
    void clear() {
        m_intervals.clear();
    }

    /// Returns the union of \p *this and \p other.
    interval_set union_with(const interval_set& other) const {
        std::array<const interval_set*, 2> args{this, &other};
        return interval_set::set_union(args | boost::adaptors::indirected);
    }

    /// Returns the intersection of \p *this and \p other.
    interval_set intersection_with(const interval_set& other) const {
        std::array<const interval_set*, 2> args{this, &other};
        return interval_set::set_intersection(args | boost::adaptors::indirected);
    }

private:
    using mut_iter = typename storage_type::iterator;

    // mutable iterators not exposed to users.
    mut_iter mut_begin() { return m_intervals.begin(); }
    mut_iter mut_end() { return m_intervals.end(); }

    /// Finds the last interval i thats begin before `point`,
    /// i.e. i.begin() <= point.
    mut_iter interval_before(point_type point) {
        const auto compare = [](point_type p, const interval_type& i) {
            return p < i.begin();
        };

        // pos is the first interval with i.begin() > point.
        const auto pos = std::upper_bound(mut_begin(), mut_end(), point, compare);
        if (pos != begin()) {
            // The predecessor is the interval we're looking for.
            return pos - 1;
        }
        // There is no such interval.
        return mut_end();
    }

    /// \copydoc interval_before()
    iterator interval_before(point_type point) const {
        return static_cast<iterator>(
                    const_cast<interval_set*>(this)->interval_before(point));
    }

    /// Iterate over the intervals, but merge adjacent intervals
    /// that do not have a gap between them (for cleaner display functionality).
    template<typename Func>
    void adjacent_merged(Func&& f) const {
        for (auto i = begin(), e = end(); i != e; ) {
            interval_type c = *i;
            while (++i != e && i->begin() == c.end() + 1) {
                c = interval_type(c.begin(), i->end());
            }
            f(c);
        }
    }

    friend std::ostream& operator<<(std::ostream& o, const interval_set& set) {
        o << "{";
        set.adjacent_merged([&](const interval_type& i) {
            o << i;
        });
        o << "}";
        return o;
    }

    void assert_invariant() {
#ifdef GEODB_DEBUG
        if (empty()) {
            return;
        }

        auto last = m_intervals.end();
        auto iter = m_intervals.begin();
        auto prev = iter++;
        for (; iter != last; ++iter, ++prev) {
            geodb_assert(!iter->overlaps(*prev), "Intervals must not overlap");
            geodb_assert(iter->begin() >= prev->end(), "intervals must be sorted");
        }
#endif
    }

    template<typename Dest>
    friend void serialize(Dest& dst, const interval_set& set) {
        using tpie::serialize;
        serialize(dst, set.m_intervals);
    }

    template<typename Src>
    friend void unserialize(Src& src, interval_set& set) {
        using tpie::unserialize;
        unserialize(src, set.m_intervals);
        set.assert_invariant();
    }

private:
    storage_type m_intervals;
};

/// A variant of \ref interval_set that automatically enforces a capacity.
/// The number of elements within a `static_interval_set` will always be
/// lesser than or equal to `Capacity`.
/// Intervals are merged as neccessary in order to keep this invariant.
template<typename T, size_t Capacity>
class static_interval_set {
    using inner_t = interval_set<T>;

    static_assert(Capacity > 0, "Capacity must not be zero");

public:
    using iterator = typename inner_t::iterator;
    using const_iterator = typename inner_t::const_iterator;

    using point_type = typename inner_t::point_type;
    using interval_type = typename inner_t::interval_type;

public:
    template<typename Range>
    static static_interval_set set_union(Range&& r) {
        inner_t set = inner_t::set_union(std::forward<Range>(r));
        return static_interval_set(std::move(set));
    }

    template<typename Range>
    static static_interval_set set_intersection(Range&& r) {
        inner_t set = inner_t::set_intersection(std::forward<Range>(r));
        return static_interval_set(std::move(set));
    }

    static constexpr size_t capacity() { return Capacity; }

public:
    static_interval_set() {}

    static_interval_set(std::initializer_list<interval_type> list)
        : inner(list)
    {
        trim();
    }

    template<typename FwdIter>
    static_interval_set(FwdIter begin, FwdIter end)
        : inner(begin, end)
    {
        trim();
    }

    explicit static_interval_set(interval_set<T> set)
        : inner(std::move(set))
    {
        trim();
    }

    iterator begin() const { return inner.begin(); }
    iterator end() const { return inner.end(); }

    /// \copydoc interval_set<T>::operator[]
    const interval_type& operator[](size_t index) const { return inner[index]; }

    /// \copydoc interval_set<T>::empty
    bool empty() const { return inner.empty(); }

    /// \copydoc interval_set<T>::size
    size_t size() const { return inner.size(); }

    /// Assign a new set of intervals, merging them if neccessary.
    template<typename FwdIter>
    void assign(FwdIter first, FwdIter last) {
        inner.assign(first, last);
        trim();
    }

    /// Adds the point to the set, merging intervals if the set becomes full.
    ///
    /// \sa interval_set::add
    bool add(point_type point) {
        bool changed = inner.add(point);
        trim();
        return changed;
    }

    /// \copydoc interval_set<T>::trim
    bool contains(point_type point) const { return inner.contains(point); }

    /// \copydoc interval_set<T>::trim
    void trim(size_t size) { inner.trim(size); }

    /// Equivalent to `trim(capacity())`.
    void trim() { trim(capacity()); }

    /// \copydoc interval_set<T>::clear
    void clear() { inner.clear(); }

    /// Returns the union of `*this` and `other`, adjusted for capacity.
    static_interval_set union_with(const static_interval_set& other) const {
        return static_interval_set(inner.union_with(other.inner));
    }

    /// Returns the intersection of `*this` and `other`, adjusted for capacity.
    static_interval_set intersection_with(const static_interval_set& other) const {
        return static_interval_set(inner.intersection_with(other.inner));
    }

    /// Returns a const view to the dynamic interval data.
    operator const interval_set<T>& () const { return inner; }

private:
    friend std::ostream& operator<<(std::ostream& o, const static_interval_set& set) {
        return o << set.inner;
    }

    template<typename Dest>
    friend void serialize(Dest& dst, const static_interval_set& set) {
        using tpie::serialize;
        serialize(dst, set.inner);
    }

    template<typename Src>
    friend void unserialize(Src& src, static_interval_set& set) {
        using tpie::unserialize;
        unserialize(src, set.inner);
    }

private:
    inner_t inner;
};

} // namespace geodb

#endif // GEODB_INTERVAL_SET_HPP
