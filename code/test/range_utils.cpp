#include <catch.hpp>

#include "geodb/utility/range_utils.hpp"

using namespace geodb;

TEST_CASE("get member reference", "[range-utils]") {
    struct s {
        int a = 0;
        double b = 0;

        int f() { return 3; }
    };

    s s1, s2;
    s1.a = 42;
    s2.b = 3.14;

    auto get_a = map_member(&s::a);
    auto get_b = map_member(&s::b);
    auto get_f = map_member(&s::f);

    REQUIRE(get_a(s1) == 42);
    REQUIRE(get_a(s2) == 0);

    REQUIRE(get_b(s1) == 0.0);
    REQUIRE(get_b(s2) == 3.14);

    REQUIRE(get_f(s1) == 3);

    // Must return correct reference type.
    static_assert(std::is_same<decltype(get_a(static_cast<s&>(s1))), int&>::value, "mutable lvalue");
    static_assert(std::is_same<decltype(get_a(static_cast<const s&>(s1))), const int&>::value, "const lvalue");
    static_assert(std::is_same<decltype(get_a(static_cast<s&&>(s1))), int&&>::value, "rvalue");

    static_assert(std::is_same<decltype(get_f(static_cast<s&>(s1))), int>::value, "returns return-type of function");
}
