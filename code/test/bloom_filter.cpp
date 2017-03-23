#include <catch.hpp>

#include "geodb/bloom_filter.hpp"

#include <algorithm>
#include <iterator>
#include <iostream>
#include <vector>

using namespace geodb;

using bloom_filter_t = bloom_filter<u64, 128>;

TEST_CASE("insert", "[bloom filter]") {
    std::vector<u64> numbers{
        1, 3, 6, 17, 65, 1346, 12357, 99344,
        1345165, 356367ULL, 1341636466485ULL,
        134163646648123125ULL,
    };

    bloom_filter_t bloom;
    for (u64 num : numbers) {
        bloom.add(num);
    }

    for (u64 num : numbers) {
        REQUIRE(bloom.contains(num));
    }
}

TEST_CASE("intersection and union", "[bloom filter]") {
    std::vector<u64> numbers_a{
        1, 3, 6, 17, 65, 1346, 12357, 99344,
        1345165,
        356367ULL,
        954547818ULL,
        1341636466485ULL,
        13416364664812312ULL,
    };
    std::vector<u64> numbers_b{
        3, 12, 65, 188, 1346, 64555, 1345165, 3564657,
        954547818ULL,
        12344656348ULL,
        1542895732490859ULL,
        13416364664812312ULL,
        3094290433892905685ULL,
    };
    std::vector<u64> numbers_intersection;
    std::set_intersection(numbers_a.begin(), numbers_a.end(),
                          numbers_b.begin(), numbers_b.end(),
                          std::back_inserter(numbers_intersection));

    std::vector<u64> numbers_union;
    std::set_union(numbers_a.begin(), numbers_a.end(),
                   numbers_b.begin(), numbers_b.end(),
                   std::back_inserter(numbers_union));

    bloom_filter_t filter_a, filter_b;
    for (u64 a : numbers_a) {
        filter_a.add(a);
    }

    for (u64 b : numbers_b) {
        filter_b.add(b);
    }

    bloom_filter_t filter_intersection = filter_a.intersection_with(filter_b);
    REQUIRE(filter_intersection == bloom_filter_t::set_intersection(
                std::vector<bloom_filter_t>{filter_a, filter_b}));

    for (u64 i : numbers_intersection) {
        REQUIRE(filter_intersection.contains(i));
    }

    bloom_filter_t filter_union = filter_a.union_with(filter_b);
    REQUIRE(filter_union == bloom_filter_t::set_union(
                std::vector<bloom_filter_t>{filter_a, filter_b}));
    for (u64 u : numbers_union) {
        REQUIRE(filter_union.contains(u));
    }
}
