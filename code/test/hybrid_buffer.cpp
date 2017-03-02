#include <catch.hpp>

#include "geodb/hybrid_buffer.hpp"

#include "boost/range/algorithm/equal.hpp"

using namespace geodb;

TEST_CASE("hybrid buffer migration", "[hybrid-buffer]") {
    hybrid_buffer<int, 512> buffer(3);

    buffer.append(1);
    buffer.append(2);
    buffer.append(3);

    REQUIRE(buffer.size() == 3);
    REQUIRE(buffer.limit() == 3);
    REQUIRE(buffer.is_internal());
    {
        std::vector<int> expected{1, 2, 3};
        REQUIRE(boost::equal(buffer, expected));
    }

    buffer.append(123);
    REQUIRE(buffer.size() == 4);
    REQUIRE(buffer.is_external());

    buffer.append(345);
    {
        std::vector<int> expected{1, 2, 3, 123, 345};
        REQUIRE(boost::equal(buffer, expected));
    }
}

TEST_CASE("hybrid buffer larger dataset", "[hybrid-buffer]") {
    hybrid_buffer<int, 4096> buffer(1024);

    static constexpr int max = 64 * 1024;
    for (int i = 0; i < max; ++i) {
        buffer.append(i * 2);
    }

    REQUIRE(buffer.is_external());
    REQUIRE(buffer.size() == max);

    int count = 0;
    for (int i : buffer) {
        if (i != count * 2) {
            FAIL("expected value " << (count * 2) << ", but found " << i);
        }
        ++count;
    }
    REQUIRE(count == max);
}
