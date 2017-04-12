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

TEST_CASE("for each sorted", "[algorithm]") {
    std::vector<int> a = { 1, 3, 4, 5 };
    std::vector<int> b = { 0, 4, 9 };
    std::vector<int> c = { 6 , 9 };
    std::vector<int> d = { };
    std::vector<int> e = { 3, 4, 5, 5 };

    const std::vector<int> expected = { 0, 1, 3, 3, 4, 4, 4, 5, 5, 5, 6, 9, 9};
    std::vector<int> result;
    for_each_sorted(std::vector<std::vector<int>>{a, b, c, d, e}, [&](int i) {
        result.push_back(i);
    });

    REQUIRE(boost::equal(result, expected));
}

TEST_CASE("for each sorted suborder", "[algorithm]") {
    struct x {
        int key;
        int order;

        bool operator<(const x& other) const {
            if (key != other.key) {
                return key < other.key;
            }
            return order < other.order;
        }

        bool operator==(const x& other) const {
            return key == other.key && order == other.order;
        }
    };

    std::vector<x> a = { {1, 2}, {2, 0}, {4, 0}, {4, 2}};
    std::vector<x> b = { {2, 1}, {4, -1} };
    std::vector<x> c = { {1, 0}, {3, 1}, {4, 1} };

    const std::vector<x> expected = {
        {1, 0}, {1, 2}, {2, 0}, {2, 1}, {3, 1}, {4, -1}, {4, 0}, {4, 1}, {4, 2}
    };
    std::vector<x> result;
    for_each_sorted(std::vector<std::vector<x>>{a, b, c}, [&](const x& v) {
        result.push_back(v);
    });

    REQUIRE(boost::equal(result, expected));
}
