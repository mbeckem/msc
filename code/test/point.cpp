#include <catch.hpp>

#include "geodb/vector.hpp"

using namespace geodb;

TEST_CASE("point construction", "[point]") {
    vector3 p;
    REQUIRE(p.x() == 0);
    REQUIRE(p.y() == 0);
    REQUIRE(p.t() == 0);

    p = vector3(1.5f, 2.5f, 3);
    REQUIRE(p.x() == 1.5f);
    REQUIRE(p.y() == 2.5f);
    REQUIRE(p.t() == 3);
}

TEST_CASE("point min/max operations", "[point]") {
    vector3 p1(1, 3, 7);
    vector3 p2(3, 1, 0);

    vector3 min = vector3::min(p1, p2);
    vector3 max = vector3::max(p1, p2);

    REQUIRE(min == vector3(1, 1, 0));
    REQUIRE(max == vector3(3, 3, 7));
}


TEST_CASE("point less_eq", "[point]") {
    struct test {
        vector3 a, b;
        bool expected;
    } tests[]
    {
        {{2, 2, 2}, {2, 2, 2}, true},
        {{2, 2, 2}, {2, 4, 2}, true},
        {{2, 2, 2}, {2, 2, 4}, true},
        {{2, 2, 2}, {4, 2, 2}, true},
        {{2, 2, 2}, {4, 4, 4}, true},

        {{2, 2, 2}, {0, 2, 2}, false},
        {{2, 2, 2}, {2, 0, 2}, false},
        {{2, 2, 2}, {2, 2, 0}, false},
    };

    for (auto& test : tests) {
        INFO("" << test.a << " <= " << test.b << ", expecting " << test.expected);
        REQUIRE(vector3::less_eq(test.a, test.b) == test.expected);
    }
}
