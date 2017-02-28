#include <catch.hpp>

#include "geodb/interval.hpp"

using namespace geodb;

using int_interval = interval<int>;

TEST_CASE("interval properties", "[interval]") {
    CHECK(int_interval(1, 2).contains(1));
    CHECK(int_interval(1, 2).contains(2));
    CHECK_FALSE(int_interval(2, 3).contains(1));
    CHECK_FALSE(int_interval(1, 2).contains(3));

    CHECK(int_interval(1, 2).contains(int_interval(1)));
    CHECK(int_interval(1, 2).contains(int_interval(1, 2)));
    CHECK(int_interval(1, 4).contains(int_interval(2, 3)));
    CHECK_FALSE(int_interval(1, 2).contains(int_interval(2, 3)));
    CHECK_FALSE(int_interval(3, 4).contains(int_interval(1, 2)));
    CHECK_FALSE(int_interval(2, 3).contains(int_interval(10, 11)));

    CHECK(int_interval(1, 3).overlaps(int_interval(1, 1)));
    CHECK(int_interval(1, 3).overlaps(int_interval(3, 5)));
    CHECK(int_interval(4, 5).overlaps(int_interval(2, 4)));
    CHECK_FALSE(int_interval(0, 1).overlaps(int_interval(2, 3)));

    CHECK(int_interval(4, 10).distance_to(11) == 1);
    CHECK(int_interval(4, 10).distance_to(99) == 89);
    CHECK(int_interval(4, 10).distance_to(0) == 4);
    CHECK(int_interval(4, 10).distance_to(4) == 0);
    CHECK(int_interval(4, 10).distance_to(6) == 0);
}
