#include <catch.hpp>

#include "geodb/hybrid_map.hpp"

#include <boost/range/algorithm/equal.hpp>

using namespace geodb;

TEST_CASE("hybrid map lookup", "[hybrid-map]") {
    hybrid_map<int, long, 512> map(2);

    map.insert(10, 15);
    {
        auto pos = map.find(10);
        REQUIRE(pos != map.end());
        REQUIRE(pos->first == 10);
        REQUIRE(pos->second == 15);
    }

    map.insert(30, 35);
    map.insert(20, 25);

    REQUIRE(map.is_external());
    {
        auto pos = map.find(20);
        REQUIRE(pos != map.end());
        REQUIRE(pos->first == 20);
        REQUIRE(pos->second == 25);
    }

    {
        auto pos = map.find(123);
        REQUIRE(pos == map.end());
    }
}

TEST_CASE("hybrid map migration", "[hybrid-map]") {
    hybrid_map<int, long, 512> map(4);
    map.insert(3, 4);
    map.insert(1, 2);
    map.insert(4, 5);
    map.insert(2, 3);

    REQUIRE(map.is_internal());
    REQUIRE(map.limit() == 4);
    REQUIRE(map.size() == 4);

    {
        std::vector<std::pair<const int, long>> expected{
            {1, 2}, {2, 3}, {3, 4}, {4, 5}
        };
        REQUIRE(boost::equal(map, expected));
    }


    map.insert(-5, -4);
    map.insert(10, 11);

    REQUIRE(map.is_external());
    REQUIRE(map.size() == 6);

    {
        std::vector<std::pair<const int, long>> expected{
            {-5, -4}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {10, 11}
        };
        REQUIRE(boost::equal(map, expected));
    }
}

TEST_CASE("hybrid map replace", "[hybrid-map]") {
    hybrid_map<int, long, 512> map(2);
    map.insert(1, 2);
    map.insert(3, 4);

    auto pos = map.find(1);
    REQUIRE(pos != map.end());
    map.replace(pos, 123);

    pos = map.find(3);
    REQUIRE(pos != map.end());
    map.replace(pos, 456);

    {
        std::vector<std::pair<const int, long>> expected{
            {1, 123}, {3, 456}
        };
        REQUIRE(boost::equal(map, expected));
    }

    map.insert(12, 13);
    map.insert(10, 11);

    pos = map.find(10);
    REQUIRE(pos != map.end());
    map.replace(pos, 121);

    pos = map.find(12);
    REQUIRE(pos != map.end());
    map.replace(pos, 169);

    {
        std::vector<std::pair<const int, long>> expected{
            {1, 123}, {3, 456}, {10, 121}, {12, 169}
        };
        REQUIRE(boost::equal(map, expected));
    }

    REQUIRE(map.size() == 4);
    REQUIRE(map.is_external());
}

TEST_CASE("hybrid map insert collions", "[hybrid-map") {
    hybrid_map<int, long, 512> map(2);
    map.insert(1, 2);
    map.insert(3, 4);
    map.insert(3, 5);
    map.insert(1, 7);

    REQUIRE(map.size() == 2);
    {
        std::vector<std::pair<const int, long>> expected{
            {1, 2}, {3, 4}
        };
        REQUIRE(boost::equal(map, expected));
    }

    map.insert(7, 8);
    map.insert(-1, 11);
    REQUIRE(map.is_external());

    map.insert(7, 123);
    map.insert(-1, 123213);

    REQUIRE(map.size() == 4);
    {
        std::vector<std::pair<const int, long>> expected{
           {-1, 11}, {1, 2}, {3, 4}, {7, 8}
        };
        REQUIRE(boost::equal(map, expected));
    }
}

TEST_CASE("hybrid map larger dataset", "[hybrid-map]") {
    hybrid_map<int, int, 4096> map(1024);

    static constexpr int max = 64 * 1024;
    for (int i = 0; i < max; ++i) {
        map.insert(i, i * 2);
    }

    REQUIRE(map.is_external());
    REQUIRE(map.size() == max);

    int count = 0;
    for (const auto& pair : map) {
        if (pair.first != count) {
            FAIL("expected key " << count << ", but found " << pair.first);
        }
        if (pair.second != count * 2) {
            FAIL("expected value " << count * 2 << ", but found " << pair.second);
        }
        ++count;
    }
    REQUIRE(count == max);
}

TEST_CASE("hybrid map custom location", "[hybrid-map]") {
    temp_dir dir("asd");

    hybrid_map<int, int, 4096> map(dir.path() / "123", 1024);
    for (int i = 0; i < 4000; ++i) {
        map.insert(i, i * 2);
    }

    REQUIRE(map.is_external());
    REQUIRE(map.size() == 4000);

    REQUIRE(fs::exists(dir.path() / "123"));
    REQUIRE(fs::exists(dir.path() / "123" / "map.tree"));
}
