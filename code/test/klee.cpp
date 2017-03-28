#include <catch.hpp>

#include "geodb/klee.hpp"

using namespace geodb;

TEST_CASE("union area 2d", "[klee]") {
    SECTION("empty input") {
        REQUIRE(union_area(std::vector<rect2d>()) == 0);
    }
    SECTION("single rectangle") {
        std::vector<rect2d> rects{
            {vector2d(5, 5), vector2d(10, 15)}
        };
        REQUIRE(union_area(rects) == 50);
    }
    SECTION("nested rectangles") {
        std::vector<rect2d> rects{
            {vector2d(5, 5), vector2d(10, 15)},
            {vector2d(6, 6), vector2d(8, 9)}
        };
        REQUIRE(union_area(rects) == 50);
    }
    SECTION("disjoint rectangles") {
        std::vector<rect2d> rects{
            {vector2d(5, 5), vector2d(10, 10)},
            {vector2d(15, 10), vector2d(25, 15)},
        };
        REQUIRE(union_area(rects) == 75);
    }
    SECTION("neighboring rectangles") {
        std::vector<rect2d> rects{
            {vector2d(5, 5), vector2d(10, 10)},
            {vector2d(10, 5), vector2d(15, 10)},
        };
        REQUIRE(union_area(rects) == 50);
    }
    SECTION("overlapping rectangles") {
        std::vector<rect2d> rects{
            {vector2d(2, -1), vector2d(3, 6)},
            {vector2d(0, 0), vector2d(5, 5)},
            {vector2d(4, 4), vector2d(6, 6)},
            {vector2d(10, 10), vector2d(12, 10.5)},
        };
        REQUIRE(union_area(rects) == 31);
    }
    SECTION("overlapping rectangles on x axis") {
        std::vector<rect2d> rects{
            {vector2d(5, 0), vector2d(10, 5)},
            {vector2d(5, 10), vector2d(10, 15)},
            {vector2d(6, 4), vector2d(8, 11)}
        };
        REQUIRE(union_area(rects) == 60);
    }
    SECTION("empty rectangles") {
        std::vector<rect2d> rects{
            {vector2d(0, 0), vector2d(5, 5)},
            {vector2d(0, 1), vector2d(0, 2)},
            {vector2d(7, 7), vector2d(7, 7)},
        };
        REQUIRE(union_area(rects) == 25);
    }
}

TEST_CASE("union area 3d", "[klee]") {
    SECTION("empty input") {
        REQUIRE(union_area(std::vector<rect3d>()) == 0);
    }
    SECTION("disjoint cubes") {
        std::vector<rect3d> cubes{
            {vector3d(5, 5, 5), vector3d(10, 10, 10)},
            {vector3d(10, 5, 5), vector3d(14, 9, 9)}
        };
        REQUIRE(union_area(cubes) == 189);
    }
    SECTION("overlapping cubes") {
        std::vector<rect3d> cubes{
            {vector3d(0, 0, 0), vector3d(10, 10, 10)},
            {vector3d(5, 5, 5), vector3d(7, 7, 7)},
            {vector3d(9, 0, 0), vector3d(12, 2, 2)}
        };

        REQUIRE(union_area(cubes) == 1008);
    }
}
