#include <catch.hpp>

#include "geodb/utility/movable_adapter.hpp"

using namespace geodb;

struct not_movable {
    not_movable(int value): value(value) {}
    not_movable(not_movable&&) = delete;
    not_movable& operator=(not_movable&&) = delete;

    int value;
};

static_assert(!std::is_move_constructible<not_movable>::value, "type is movable");

TEST_CASE("make type movable", "[movable-adapter]") {
    movable_adapter<not_movable> p = make_movable<not_movable>(123);
    REQUIRE(p->value == 123);

    movable_adapter<not_movable> q = std::move(p);
    REQUIRE(q->value == 123);

    static_assert(std::is_move_constructible<movable_adapter<not_movable>>::value, "must be move-constructible");
    static_assert(movable_adapter<not_movable>::wrapped, "must be wrapped via unique-ptr");
}

struct movable {
    movable(int value): value(value) {}

    int value;
};

static_assert(std::is_move_constructible<movable>::value, "type is not movable");

TEST_CASE("noop for movable types", "[movable-adapter]") {
    movable_adapter<movable> p = make_movable<movable>(123);
    REQUIRE(p->value == 123);

    movable_adapter<movable> q = std::move(p);
    REQUIRE(q->value == 123);

    static_assert(std::is_move_constructible<movable_adapter<movable>>::value, "must be move-constructible");
    static_assert(!movable_adapter<movable>::wrapped, "type must not be wrapped");
}
