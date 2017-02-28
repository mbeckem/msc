#include <catch.hpp>

#include "geodb/algorithm.hpp"

using namespace geodb;

TEST_CASE("k smallest", "[algorithm]") {
    std::vector<int> in{99, 3, 17, 23, 2, 5, 55, 7};

    struct test {
        size_t k;
        std::vector<int> expect;
    };
    test tests[] = {
        { 1, { 2 }},
        { 2, { 2, 3 }},
        { 3, { 2, 3, 5 }},
        { 4, { 2, 3, 5, 7 }},
        { 5, { 2, 3, 5, 7, 17 }},
        { 6, { 2, 3, 5, 7, 17, 23 }},
        { 7, { 2, 3, 5, 7, 17, 23, 55 }},
        { 8, { 2, 3, 5, 7, 17, 23, 55, 99}},
    };
    for (test& t : tests) {
        INFO("k = " << t.k);

        std::vector<int> result(t.k);
        k_smallest(in, t.k, result);

        REQUIRE(boost::equal(t.expect, result));
    }
}
