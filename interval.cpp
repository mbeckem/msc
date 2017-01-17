#include "interval.hpp"

#include <limits>
#include <ostream>

namespace geodb {

std::vector<interval> summarize(const std::vector<interval>& input, size_t lambda) {
    const size_t size = input.size();

    Expects(size > 0);
    Expects(lambda > 0);
    Expects(lambda <= input.size());

    if (lambda == size) {
        return input;
    }

    std::vector<uint32_t> cost(size * lambda);

    // Returns COST[i, j] where i is the index into the interval list
    // and j is the current value for lambda.
    // at(i, j) is the cost of summarizing the intervals 0, ..., i
    // into j+1 intervals.
    auto at = [&](size_t i, size_t j) -> auto& {
        const size_t index = i * lambda + j;
        Expects(index < cost.size());
        return cost[index];
    };

    // Returns the cost of merging the interval at index i
    // into its predecessor.
    auto k = [&](size_t i) {
        Expects(i > 0 && i < size);
        return input[i].begin() - input[i - 1].end();
    };

    // Initialize base case (merge remaining invervals into one).
    at(0, 0) = 0;
    for (size_t i = 1; i < size; ++i) {
        at(i, 0) = at(i - 1, 0) + k(i);
    }

    // Impossible combinations where the number of requested intervals
    // is greater than the number of available ones.
    for (size_t i = 0; i < lambda; ++i) {
        for (size_t j = i + 1; j < lambda; ++j) {
            constexpr uint32_t inf = std::numeric_limits<uint32_t>::max();
            at(i, j) = inf;
        }
    }

    // Compute optimal cost for all other values.
    for (size_t i = 1; i < size; ++i) {
        // Only compute values at or below the main diagonal.
        const size_t max_j = std::min(lambda, i + 1);
        for (size_t j = 1; j < max_j; ++j) {
            assert(i >= j);
            at(i, j) = std::min(at(i - 1, j) + k(i),
                                at(i - 1, j - 1));
        }
    }

    // Backtrack through the cost matrix and assemble the summarized
    // intervals.
    std::vector<interval> result(lambda);
    size_t e = size - 1;
    size_t j = lambda - 1;
    while (j > 0) {
        // input[e] is the last interval in its segment.
        // iterate in reverse to find the first interval,
        // i.e. the one that has a value of cost[i-1, j-1].
        size_t b = e;
        while (b > 0 && at(b, j) != at(b - 1, j - 1)) {
            --b;
        }

        result[j] = interval(input[b].begin(), input[e].end());

        assert(b > 0);
        e = b - 1;
        j = j - 1;
    }
    result[0] = interval(input[0].begin(), input[e].end());
    return result;
}

std::ostream& operator<<(std::ostream& o, const interval& i) {
    return o << "[" << i.begin() << ", " << i.end() << "]";
}

} // namespace geodb
