#include <catch.hpp>

#include "geodb/utility/shared_values.hpp"

using namespace geodb;

struct value_t : boost::noncopyable {
    static int alive;
    int i;

    value_t(int i): i(i) {
        ++alive;
    }

    value_t(value_t&& other): i(other.i) {
        ++alive;
    }

    ~value_t() {
        --alive;
    }
};

int value_t::alive = 0;

using shared_t = shared_instances<int, value_t>;

TEST_CASE("shared values open", "[shared-values]") {
    shared_t s;
    int opens = 0;
    auto open = [&](int i) {
        return s.open(i, [&]() { ++opens; return value_t(i); });
    };

    REQUIRE(s.empty());
    REQUIRE(s.get(123) == nullptr);

    {
        shared_t::pointer p1;
        shared_t::const_pointer p2;
        shared_t::pointer p3 = s.convert(p2);

        REQUIRE(p1 == nullptr);
        REQUIRE(p2 == nullptr);
        REQUIRE(p3 == nullptr);
        REQUIRE(p1 == p2);
        REQUIRE(p1 == p3);
        REQUIRE(!p1);
        REQUIRE(!p2);
        REQUIRE(!p3);
    }

    {
        auto h1 = open(3);
        auto h2 = h1;
        auto h3 = open(3);

        REQUIRE(h1);
        REQUIRE(h1 != nullptr);
        REQUIRE(h1->i == 3);
        REQUIRE(h1 == h2);
        REQUIRE(h1 == h3);
        REQUIRE(opens == 1);
        REQUIRE(value_t::alive == 1);

        auto h4 = open(4);
        shared_t::const_pointer h5 = h4;
        shared_t::pointer h6 = s.convert(h5);

        REQUIRE(h4);
        REQUIRE(h4 != nullptr);
        REQUIRE(h4->i == 4);
        REQUIRE(h4 != h1);
        REQUIRE(h4 == h5);
        REQUIRE(h4 == h6);
        REQUIRE(opens == 2);
        REQUIRE(value_t::alive == 2);
    }

    REQUIRE(value_t::alive == 0);
}
