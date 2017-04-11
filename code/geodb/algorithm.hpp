#ifndef GEODB_ALGORITHM_HPP
#define GEODB_ALGORITHM_HPP

#include "geodb/common.hpp"

#include <boost/range/concepts.hpp>
#include <boost/range/metafunctions.hpp>
#include <boost/range/sub_range.hpp>

#include <algorithm>
#include <map>
#include <queue>
#include <vector>

/// \file
/// Some generic algorithms.

namespace geodb {

/// Invokes the function `f` for every token in `r`.
/// The end of a token is reached when
///     - the end of the range was encountered
///     - the predicate `p' returns `true`
///
/// `p` will be invoked with the iterator to the current element,
/// which is always valid.
///
/// `f` will be invoked for each token with an argument of type
/// `boost::sub_range<Range>`.
template<typename Range, typename Predicate, typename Function>
void for_each_token_if(Range&& r, Predicate&& p, Function&& f) {
    using iterator = typename boost::range_iterator<Range>::type;
    using sub_range = boost::sub_range<Range>;

    iterator first = boost::begin(r);
    iterator last = boost::end(r);
    if (first == last)
        return;

    while (1) {
        // There is at least one more token, although it may be empty.
        auto pos = first;

        // Find the end of the current token.
        // This is either the end of the range or the item
        // for which p returns true.
        // If p returns true, at least one more token must follow.
        while (1) {
            if (pos == last) {
                f(sub_range(first, pos));
                return;
            }
            if (p(pos)) {
                f(sub_range(first, pos));
                ++pos; // skip separator in output.
                break;
            }
            ++pos;
        }

        first = pos;
    }
}

/// Invokes the function `f` for every token in `r`.
/// Tokens are separated by the seperator element.
template<typename Range, typename Function>
void for_each_token(Range&& r, typename boost::range_value<Range>::type separator, Function&& f) {
    auto predicate = [&](const auto& iter) {
        return *iter == separator;
    };
    return for_each_token_if(std::forward<Range>(r), predicate, std::forward<Function>(f));
}

/// Takes the `k` smallest entries (according to `cmp`) and puts then into `out`.
/// The first `k` elements of `out` will be in sorted order.
/// Runtime complexity: O(n * log(k)) comparisons.
///
/// \pre `k > 0`.
/// \pre `r` must have at least `k` elements.
/// \pre `out` must have space for `k` elements.
template<typename Range, typename OutRange, typename Compare>
void k_smallest(Range&& r, size_t k, OutRange& out, Compare&& cmp) {
    BOOST_CONCEPT_ASSERT(( boost::RandomAccessRangeConcept<OutRange> ));

    geodb_assert(k > 0, "invalid k");
    geodb_assert(boost::size(out) >= k, "out range too small");

    auto heap_first = boost::begin(out);
    auto heap_last = heap_first + k;

    auto first = boost::const_begin(r);
    auto last = boost::const_end(r);

    // Push first k elements into the output heap.
    // Invariant: heap stores the k smallest elements in
    // the visited part of the range.
    {
        auto first_k = first + k;
        std::copy(first, first_k, heap_first);
        std::make_heap(heap_first, heap_last, cmp);
        first = first_k;
    }

    // If the next element is smaller than the max,
    // then replace the max with it. Otherwise: the element is
    // definitely not among the smallest k.
    for (; first != last; ++first) {
        if(cmp(*first, *heap_first)) {
            std::pop_heap(heap_first, heap_last, cmp);
            heap_last[-1] = *first;
            std::push_heap(heap_first, heap_last, cmp);
        }
    }
    std::sort_heap(heap_first, heap_last, cmp);
}

/// Uses `operator<` for comparisons.
template<typename Range, typename OutRange>
void k_smallest(Range&& r, size_t k, OutRange&& out) {
    k_smallest(r, k, out, std::less<>());
}

/// Invokes the function object `f` for every pair of adjacent elements
/// in `r`.
///
/// For example, if `r = [1, 2, 3]`, this algorithm will
/// call `f(1, 2)` and `f(2, 3)`.
template<typename FwdRange, typename Function>
void for_each_adjacent(FwdRange&& r, Function&& f) {
    BOOST_CONCEPT_ASSERT(( boost::ForwardRangeConcept<FwdRange> ));

    auto current = boost::begin(r);
    auto last = boost::end(r);

    if (current != last) {
        auto previous = current;
        ++current;
        for (; current != last; ++current, ++previous) {
            f(*previous, *current);
        }
    }
}

/// Iterate over a range of ranges and treat their elements
/// as one large, contiguous sequence.
/// The elements will be visited in sorted order.
///
/// Uses a binary heap internally to track the position in each
/// range and thus uses O(m) additional space (where m is the number of ranges).
/// The algorithm thus takes O(n * log m) time, where n is the number of total
/// elements over all ranges.
///
/// \param ranges
///     A range of ranges. Every individual range must be sorted.
/// \param fn
///     A function that will be invoked for every element in the ranges.
/// \param comp
///     A comparison function to compare individual elements of the ranges.
///     Defaults to std::less.
template<typename Ranges, typename Function, typename Comp = std::less<>>
void for_each_sorted(const Ranges& ranges, Function&& fn, Comp&& comp = Comp()) {
    using nested_range = typename boost::range_value<const Ranges>::type;
    using nested_iterator = typename boost::range_const_iterator<nested_range>::type;

    static_assert(std::is_lvalue_reference<typename boost::range_reference<const Ranges>::type>::value,
                  "nested ranges must be lvalues (i.e. no temporaries).");

    struct cursor {
        nested_iterator current;
        nested_iterator end;

        cursor(nested_iterator current, nested_iterator end)
            : current(current), end(end)
        {}
    };

    struct cursor_compare {
        Comp& value_comp;

        cursor_compare(Comp& value_comp): value_comp(value_comp) {}

        bool operator()(const cursor& a, const cursor& b) const {
            return value_comp(*b.current, *a.current);
        }
    };

    auto heap = [&]{
        std::vector<cursor> cursors;
        cursors.reserve(boost::size(ranges));

        // Add a cursor for every range.
        for (auto&& range : ranges) {
            cursor c(boost::const_begin(range), boost::const_end(range));
            if (c.current != c.end) {
                cursors.push_back(std::move(c));
            }
        }
        return std::priority_queue<cursor,
                std::vector<cursor>,
                cursor_compare>(cursor_compare(comp), std::move(cursors));
    }();

    // Read the min value until we have seen everything.
    while (!heap.empty()) {
        cursor c = heap.top();
        heap.pop();

        fn(*c.current);
        ++c.current;
        if (c.current != c.end) {
            heap.push(std::move(c));
        }
    }
}

/// Creates a map of groups and their values.
/// All elements with the same key value will be put into the same group.
/// Relative order of elements within a group is preserved.
///
/// \param r        The input range.
/// \param key      A function that maps an element of `r` to a key value.
///
/// \return         A `map<key_type, std::vector<value_type>>` where `key_type`
///                 is the return type of `key`.
template<typename Range, typename KeyFunc>
auto group_by_key(Range&& r, KeyFunc&& key) {
    using value_type = typename boost::range_value<Range>::type;
    using key_type = decltype(key(std::declval<const value_type&>()));

    std::map<key_type, std::vector<value_type>> result;
    for (const auto& item : r) {
        result[key(item)].push_back(item);
    }
    return result;
}

/// Assign a range to a container.
template<typename Container, typename Range>
void assign(Container&& c, const Range& r) {
    c.assign(boost::begin(r), boost::end(r));
}

/// Append a range to a container.
template<typename Container, typename Range>
void append(Container&& c, const Range& r) {
    c.insert(c.end(), boost::begin(r), boost::end(r));
}

/// True iff `r` contains `t`.
template<typename Range, typename T>
bool contains(const Range& r, const T& t) {
    return std::find(boost::begin(r), boost::end(r), t) != boost::end(r);
}

/// True iff `p(x)` is true for every element `x` of `r`.
template<typename Range, typename Predicate>
bool all_of(const Range& r, Predicate p) {
    return std::any_of(boost::begin(r), boost::end(r), p);
}

/// Copy the contents of the given range into a new vector.
template<typename Range>
std::vector<typename boost::range_value<Range>::type> to_vector(const Range& r) {
    return std::vector<typename boost::range_value<Range>::type>(boost::begin(r), boost::end(r));
}

/// Removes the element at position `pos` from the vector `v`
/// by swapping the last element of `v` into the position
/// and decreasing the size of the vector.
/// In other words, the order of elements will not be maintained.
///
/// \pre `pos` points to an element, i.e. it is not `v.end()`.
template<typename Vector>
void fast_remove(Vector&& v, typename Vector::iterator pos) {
    geodb_assert(pos != v.end(), "trying to remove the end iterator");
    goedb_assert(!v.empty(), "trying to remove from an empty vector");
    const auto last = v.end() - 1;
    if (pos != last) {
        std::iter_swap(pos, last);
    }
    v.pop_back();
}

} // namespace geodb

#endif // GEODB_ALGORITHM_HPP
