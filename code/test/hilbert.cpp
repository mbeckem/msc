#include <catch.hpp>

#include "geodb/hilbert.hpp"

#include <iostream>

using namespace geodb;

// 3 Dimensions
using curve = hilbert_curve<3, 3>;
using bitset_t = curve::bitset_t;

TEST_CASE("bit rotation", "[hilbert]") {
    bitset_t p1(0b100);

    REQUIRE(curve::rotate_right(p1, 0) == p1);
    REQUIRE(curve::rotate_right(p1, 1) == 0b010);
    REQUIRE(curve::rotate_right(p1, 2) == 0b001);
    REQUIRE(curve::rotate_right(p1, 3) == p1);

    REQUIRE(curve::rotate_left(p1, 0) == p1);
    REQUIRE(curve::rotate_left(p1, 1) == 0b001);
    REQUIRE(curve::rotate_left(p1, 2) == 0b010);
    REQUIRE(curve::rotate_left(p1, 3) == p1);

    bitset_t p2(0b011);
    REQUIRE(curve::rotate_right(p2, 0) == p2);
    REQUIRE(curve::rotate_right(p2, 1) == 0b101);
    REQUIRE(curve::rotate_right(p2, 2) == 0b110);
    REQUIRE(curve::rotate_right(p2, 3) == p2);

    REQUIRE(curve::rotate_left(p2, 0) == p2);
    REQUIRE(curve::rotate_left(p2, 1) == 0b110);
    REQUIRE(curve::rotate_left(p2, 2) == 0b101);
    REQUIRE(curve::rotate_left(p2, 3) == p2);
}

TEST_CASE("gray code", "[hilbert]") {
    REQUIRE(curve::gray_code(0) == 0b000);
    REQUIRE(curve::gray_code(1) == 0b001);
    REQUIRE(curve::gray_code(2) == 0b011);
    REQUIRE(curve::gray_code(3) == 0b010);
    REQUIRE(curve::gray_code(4) == 0b110);
    REQUIRE(curve::gray_code(5) == 0b111);
    REQUIRE(curve::gray_code(6) == 0b101);
    REQUIRE(curve::gray_code(7) == 0b100);
}

TEST_CASE("reverse gray code", "[hilbert]") {
    for (u32 i = 0; i < 8; ++i) {
        INFO("i = " << i);

        bitset_t gray_code = curve::gray_code(i);
        u32 reversed = curve::gray_code_inverse(gray_code);
        REQUIRE(i == reversed);
    }
}

TEST_CASE("hypercube entry points", "[hilbert]") {
    REQUIRE(curve::entry(0) == 0b000);
    REQUIRE(curve::entry(1) == 0b000);
    REQUIRE(curve::entry(2) == 0b000);
    REQUIRE(curve::entry(3) == 0b011);
    REQUIRE(curve::entry(4) == 0b011);
    REQUIRE(curve::entry(5) == 0b110);
    REQUIRE(curve::entry(6) == 0b110);
    REQUIRE(curve::entry(7) == 0b101);
}

TEST_CASE("hybercube exit points", "[hilbert]") {
    REQUIRE(curve::exit(0) == 0b001);
    REQUIRE(curve::exit(1) == 0b010);
    REQUIRE(curve::exit(2) == 0b010);
    REQUIRE(curve::exit(3) == 0b111);
    REQUIRE(curve::exit(4) == 0b111);
    REQUIRE(curve::exit(5) == 0b100);
    REQUIRE(curve::exit(6) == 0b100);
    REQUIRE(curve::exit(7) == 0b100);
}

TEST_CASE("subcube properties", "[hilbert]") {
    for (u32 i = 0; i < curve::cubes - 1; ++i) {
        INFO("index = " << i);

        bitset_t computed = bitset_t(curve::gray_code(i) ^ bitset_t(1 << curve::changed_dimension(i)));
        bitset_t expected = curve::gray_code(i + 1);
        REQUIRE(computed == expected);
    }

    for (u32 i = 0; i < curve::cubes; ++i) {
        INFO("index = " << i);

        {
            bitset_t result = curve::transform(curve::entry(i), curve::change(i), curve::entry(i));
            REQUIRE(result == 0);
        }
        {
            bitset_t result = curve::transform(curve::entry(i), curve::change(i), curve::exit(i));
            REQUIRE(result == (1 << (curve::dimension - 1)));
        }
    }

    for (u32 i = 0; i < curve::cubes; ++i) {
        INFO("index = "  << i);

        bitset_t result = curve::entry(i) ^ bitset_t(1 << curve::change(i));
        REQUIRE(result == curve::exit(i));
    }

    for (u32 i = 0; i < curve::cubes; ++i) {
        INFO("i = " << i);
        for (u32 j = 0; j < curve::cubes; ++j) {
            INFO("j = " << j);

            bitset_t e = curve::entry(i);
            u32 d = curve::change(i);

            bitset_t b(j);
            bitset_t e1 = curve::transform(e, d, curve::transform_inverse(e, d, b));
            REQUIRE(e1 == b);

            bitset_t e2 = curve::transform_inverse(e, d, curve::transform(e, d, b));
            REQUIRE(e2 == b);
        }
    }
}

TEST_CASE("hilbert index round trip", "[hilbert]") {
    for (curve::index_t index = 0; index < curve::index_count; ++index) {
        curve::point_t point = curve::hilbert_index_inverse(index);
        curve::index_t computed = curve::hilbert_index(point);
        if (index != computed) {
            FAIL("Expected " << index << ", got " << computed);
        }
    }
}
