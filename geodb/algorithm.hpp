#ifndef ALGORITHM_HPP
#define ALGORITHM_HPP

#include "geodb/common.hpp"

#include <boost/range/concepts.hpp>
#include <boost/range/metafunctions.hpp>
#include <boost/range/sub_range.hpp>

#include <algorithm>
#include <map>
#include <vector>

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

} // namespace geodb

#endif // ALGORITHM_HPP
