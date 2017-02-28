#include <catch.hpp>

#include "geodb/bounding_box.hpp"

using namespace geodb;

TEST_CASE("bounding box extending", "[bounding box]") {
    bounding_box bb{point(5, 5, 5), point(10, 10, 10)};

    // NOOP
    REQUIRE(bb == bb.extend(bb));

    // NOOP
    bounding_box contained{point(6, 6, 6), point(7, 7, 7)};
    REQUIRE(bb.extend(contained) == bb);

    {
        bounding_box intersect{point(9, 9, 9), point(12, 13, 10)};
        bounding_box expected{point(5, 5, 5), point(12, 13, 10)};
        REQUIRE(bb.extend(intersect) == expected);
    }

    {
        bounding_box disjoint{point(100, 100, 100), point(102, 103, 104)};
        bounding_box expected{point(5, 5, 5), point(102, 103, 104)};
        REQUIRE(bb.extend(disjoint) == expected);
    }
}

TEST_CASE("bounding box contains", "[bounding box]") {
    bounding_box bb{point(5, 5, 5), point(10, 10, 10)};

    bounding_box contained{point(6, 6, 6), point(10, 10, 7)};
    REQUIRE(bb.contains(contained));

    bounding_box overlap{point(6, 6, 6), point(11, 10, 10)};
    REQUIRE(!bb.contains(overlap));

    bounding_box disjoint{point(0, 0, 0), point(1, 1, 1)};
    REQUIRE(!bb.contains(disjoint));
}

TEST_CASE("bounding box intersection", "[bounding-box]") {
    struct test {
        bounding_box a;
        bounding_box b;
        bool expected;
    };

    test tests[] = {
        {{point(5, 5, 5), point(10, 10, 10)}, {point(9, 9, 9), point(11, 11, 11)}, true},
        {{point(5, 5, 5), point(10, 10, 10)}, {point(0, 0, 0), point(5, 5, 5)}, true},
        {{point(5, 5, 5), point(10, 10, 10)}, {point(6, 6, 6), point(7, 7, 7)}, true},
        {{point(5, 5, 5), point(10, 10, 10)}, {point(0, 0, 0), point(15, 15, 15)}, true},
        {{point(5, 5, 5), point(10, 10, 10)}, {point(11, 11, 11), point(12, 12, 12)}, false},
        {{point(5, 5, 5), point(10, 10, 10)}, {point(0, 0, 0), point(4, 4, 4)}, false},

        // under, over
        {{point(5, 5, 5), point(10, 10, 10)}, {point(5, 5, 3), point(10, 10, 4)}, false},
        {{point(5, 5, 5), point(10, 10, 10)}, {point(5, 5, 11), point(10, 10, 12)}, false},

        // left, right
        {{point(5, 5, 5), point(10, 10, 10)}, {point(3, 5, 5), point(4, 10, 10)}, false},
        {{point(5, 5, 5), point(10, 10, 10)}, {point(11, 5, 5), point(12, 10, 10)}, false},

        // before, behind
        {{point(5, 5, 5), point(10, 10, 10)}, {point(5, 3, 5), point(10, 4, 10)}, false},
        {{point(5, 5, 5), point(10, 10, 10)}, {point(5, 11, 5), point(10, 12, 10)}, false},

    };

    for (test& t : tests) {
        INFO("a = " << t.a);
        INFO("b = " << t.b);

        CHECK(t.a.intersects(t.b) == t.expected);
    }
}
