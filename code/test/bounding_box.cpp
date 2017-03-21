#include <catch.hpp>

#include "geodb/bounding_box.hpp"

using namespace geodb;

TEST_CASE("bounding box extending", "[bounding box]") {
    bounding_box bb{vector3(5, 5, 5), vector3(10, 10, 10)};

    // NOOP
    REQUIRE(bb == bb.extend(bb));

    // NOOP
    bounding_box contained{vector3(6, 6, 6), vector3(7, 7, 7)};
    REQUIRE(bb.extend(contained) == bb);

    {
        bounding_box intersect{vector3(9, 9, 9), vector3(12, 13, 10)};
        bounding_box expected{vector3(5, 5, 5), vector3(12, 13, 10)};
        REQUIRE(bb.extend(intersect) == expected);
    }

    {
        bounding_box disjoint{vector3(100, 100, 100), vector3(102, 103, 104)};
        bounding_box expected{vector3(5, 5, 5), vector3(102, 103, 104)};
        REQUIRE(bb.extend(disjoint) == expected);
    }
}

TEST_CASE("bounding box contains", "[bounding box]") {
    bounding_box bb{vector3(5, 5, 5), vector3(10, 10, 10)};

    bounding_box contained{vector3(6, 6, 6), vector3(10, 10, 7)};
    REQUIRE(bb.contains(contained));

    bounding_box overlap{vector3(6, 6, 6), vector3(11, 10, 10)};
    REQUIRE(!bb.contains(overlap));

    bounding_box disjoint{vector3(0, 0, 0), vector3(1, 1, 1)};
    REQUIRE(!bb.contains(disjoint));
}

TEST_CASE("bounding box intersection test", "[bounding box]") {
    struct test {
        bounding_box a;
        bounding_box b;
        bool expected;
    };

    test tests[] = {
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(9, 9, 9), vector3(11, 11, 11)}, true},
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(0, 0, 0), vector3(5, 5, 5)}, true},
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(6, 6, 6), vector3(7, 7, 7)}, true},
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(0, 0, 0), vector3(15, 15, 15)}, true},
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(11, 11, 11), vector3(12, 12, 12)}, false},
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(0, 0, 0), vector3(4, 4, 4)}, false},

        // under, over
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(5, 5, 3), vector3(10, 10, 4)}, false},
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(5, 5, 11), vector3(10, 10, 12)}, false},

        // left, right
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(3, 5, 5), vector3(4, 10, 10)}, false},
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(11, 5, 5), vector3(12, 10, 10)}, false},

        // before, behind
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(5, 3, 5), vector3(10, 4, 10)}, false},
        {{vector3(5, 5, 5), vector3(10, 10, 10)}, {vector3(5, 11, 5), vector3(10, 12, 10)}, false},

    };

    for (test& t : tests) {
        INFO("a = " << t.a);
        INFO("b = " << t.b);

        CHECK(t.b.intersects(t.a) == t.expected);
        CHECK(t.a.intersects(t.b) == t.expected);
    }
}

TEST_CASE("bounding box intersection", "[bounding box]") {
    struct test {
        bounding_box a;
        bounding_box b;
        bounding_box i;
    };

    test tests[] = {
        {
            bounding_box(vector3(0, 0, 0), vector3(1, 1, 1)),
            bounding_box(vector3(2, 2, 2), vector3(3, 3, 3)),
            bounding_box()
        },
        {
            bounding_box(vector3(10, 11, 12), vector3(15, 16, 17)),
            bounding_box(vector3(14, 14, 14), vector3(20, 20, 20)),
            bounding_box(vector3(14, 14, 14), vector3(15, 16, 17))
        },
        {
            bounding_box(vector3(5, 5, 5), vector3(15, 7, 8)),
            bounding_box(vector3(9, 5, 5), vector3(10, 10, 10)),
            bounding_box(vector3(9, 5, 5), vector3(10, 7, 8))
        }
    };

    for (test& t : tests) {
        INFO("a = " << t.a);
        INFO("b = " << t.b);
        INFO("expected = " << t.i);

        CHECK(t.b.intersection(t.a) == t.i);
        CHECK(t.a.intersection(t.b) == t.i);
    }
}
