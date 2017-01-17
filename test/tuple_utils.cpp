#include <catch.hpp>

#include "geodb/utility/tuple_utils.hpp"

#include <vector>

using namespace geodb;

TEST_CASE("enumerate with constants", "[enumerate]") {
    std::vector<size_t> seen;
    std::vector<size_t> expected{0, 1, 2, 3};

    enumerate<4>([&](auto i) {
        seen.push_back(i());
    });
    REQUIRE(seen.size() == 4);
    REQUIRE(std::equal(seen.begin(), seen.end(), expected.begin()));
}
