#include <catch.hpp>

#include "geodb/trajectory.hpp"

using namespace geodb;

TEST_CASE("bounding box for trajectory units", "[bounding box]") {
    label_type label(1);

    struct test {
        trajectory_unit input;
        bounding_box expected;
    } tests[]
    {
        {
            { point(1, 2, 0), point(5, 7, 1), label },
            { point(1, 2, 0), point(5, 7, 1) }
        }, {
            { point(1, 7, 0), point(5, 2, 1), label },
            { point(1, 2, 0), point(5, 7, 1) }
        }, {
            { point(5, 2, 0), point(1, 7, 1), label },
            { point(1, 2, 0), point(5, 7, 1) }
        }, {
            { point(5, 7, 0), point(1, 2, 1), label },
            { point(1, 2, 0), point(5, 7, 1) }
        }, {
            { point(5, 5, 1), point(6, 7, 0), label },
            { point(5, 5, 0), point(6, 7, 1) }
        },
    };

    for (auto& test : tests) {
        INFO("line: " << test.input.start << ", " << test.input.end);
        REQUIRE(test.input.get_bounding_box() == test.expected);
    }
}
