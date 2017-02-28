#include <catch.hpp>

#include "geodb/interval_set.hpp"

#include <sstream>
#include <vector>

using namespace geodb;

using small_set = static_interval_set<int, 3>;

TEST_CASE("interval set normal insertion", "[interval-set]") {

    small_set set;
    REQUIRE(set.begin() == set.end());
    REQUIRE(set.empty());
    REQUIRE(set.size() == 0);
    REQUIRE(set.capacity() == 3);

    REQUIRE(set.add(5));
    REQUIRE(set.size() == 1);
    REQUIRE(set.begin() != set.end());
    REQUIRE(set[0] == 5);

    REQUIRE_FALSE(set.add(5));

    REQUIRE(set.add(6));
    REQUIRE(set.size() == 2);
    REQUIRE(set[0] == 5);
    REQUIRE(set[1] == 6);

    REQUIRE(set.add(11));
    REQUIRE(set.size() == 3);
    REQUIRE(set[0] == 5);
    REQUIRE(set[1] == 6);
    REQUIRE(set[2] == 11);
}

TEST_CASE("interval set range constructor", "[interval-set]") {
    std::initializer_list<interval<int>> range{11, {15, 25}};

    small_set set(range.begin(), range.end());
    REQUIRE(set.size() == 2);
    REQUIRE(set[0] == 11);
    REQUIRE(set[1] == interval<int>(15, 25));
}

TEST_CASE("interval set contains", "[interval-set]") {
    {
        small_set set{{5, 6}, {11}, {12}};

        REQUIRE(set.contains(5));
        REQUIRE(set.contains(6));
        REQUIRE(set.contains(11));
        REQUIRE(set.contains(12));

        REQUIRE_FALSE(set.contains(0));
        REQUIRE_FALSE(set.contains(4));
        REQUIRE_FALSE(set.contains(7));
        REQUIRE_FALSE(set.contains(10));
        REQUIRE_FALSE(set.contains(13));
    }

    {
        small_set set{{11, 13}};
        REQUIRE(set.contains(11));
        REQUIRE(set.contains(12));
        REQUIRE(set.contains(13));
        REQUIRE_FALSE(set.contains(14));
    }
}

TEST_CASE("interval set at capacity", "[interval-set]") {
    SECTION("merge first (1)") {
        small_set set{5, 6, 11};
        set.add(12);

        REQUIRE(set.size() == 3);
        REQUIRE(set[0] == interval<int>(5, 6));
        REQUIRE(set[1] == 11);
        REQUIRE(set[2] == 12);
    }

    SECTION("merge first (2)") {
        small_set set{5, 6, 11};
        set.add(7);

        REQUIRE(set.size() == 3);
        REQUIRE(set[0] == interval<int>(5, 6));
        REQUIRE(set[1] == 7);
        REQUIRE(set[2] == 11);
    }

    SECTION("merge first (3)") {
        small_set set{5, 6, 11};
        set.add(4);

        REQUIRE(set.size() == 3);
        REQUIRE(set[0] == interval<int>(4, 5));
        REQUIRE(set[1] == interval<int>(6));
        REQUIRE(set[2] == interval<int>(11));
    }

    SECTION("merge mid") {
        small_set set{5, 6, 11};
        set.add(1);

        REQUIRE(set.size() == 3);
        REQUIRE(set[0] == 1);
        REQUIRE(set[1] == interval<int>(5, 6));
        REQUIRE(set[2] == 11);
    }

    SECTION("merge end") {
        small_set set{3, 6, 11};
        set.add(13);

        REQUIRE(set.size() == 3);
        REQUIRE(set[0] == 3);
        REQUIRE(set[1] == 6);
        REQUIRE(set[2] == interval<int>(11, 13));
    }
}

TEST_CASE("interval merging for large intervals", "[interval-set]") {
    small_set set{{34, 59}, {101, 161}, 244};

    set.add(245);
    REQUIRE(set[0] == interval<int>(34, 59));
    REQUIRE(set[1] == interval<int>(101, 161));
    REQUIRE(set[2] == interval<int>(244, 245));

    set.add(999);
    REQUIRE(set[0] == interval<int>(34, 161));
    REQUIRE(set[1] == interval<int>(244, 245));
    REQUIRE(set[2] == interval<int>(999));
}

//TEST_CASE("merge overlapping intervals", "[interval-set]") {
//    std::vector<interval<int>> list{1, 2, {55, 88}, {53, 59}, 3,
//                                    {44, 90}, {100, 105}, {102, 106},
//                                    {96, 102}, 10};
//    std::vector<interval<int>> merged = detail::merge_overlapping(list);

//    REQUIRE(merged.size() == 6);
//    REQUIRE(merged[0] == 1);
//    REQUIRE(merged[1] == 2);
//    REQUIRE(merged[2] == 3);
//    REQUIRE(merged[3] == 10);
//    REQUIRE(merged[4] == interval(44, 90));
//    REQUIRE(merged[5] == interval(96, 106));
//}

TEST_CASE("interval events for multiple ranges", "[interval-set]") {
    std::vector<static_interval_set<int, 5>> sets;
    sets.push_back({1, 3, {5, 11}, {12, 15}});
    sets.push_back({{2, 7}, {11, 13}, 15});
    sets.push_back({3, 4, {14, 16}});

    using detail::interval_event;
    using detail::interval_events;

    static const auto open = interval_event<int>::open;
    static const auto close = interval_event<int>::close;

    std::vector<interval_event<int>> expected{
        {open, 1}, {close, 1}, {open, 2}, {open, 3}, {open, 3},
        {close, 3}, {close, 3}, {open, 4}, {close, 4}, {open, 5},
        {close, 7}, {open, 11}, {close, 11}, {open, 12}, {close, 13},
        {open, 14}, {open, 15}, {close, 15}, {close, 15}, {close, 16}
    };
    std::vector<interval_event<int>> result;
    interval_events(sets, [&](interval_event<int> e) {
        result.push_back(e);
    });

    REQUIRE(boost::range::equal(expected, result));
}

TEST_CASE("correct merging of gaps between intervals", "[interval-set]") {
    using iterator = typename std::vector<interval<int>>::iterator;

    std::vector<interval<int>> intervals{
        1, {3, 4}, 9, {11, 55}, 57, {60, 61}, 81
    };

    {
        auto result = intervals;
        auto expected = intervals;
        std::vector<iterator> empty;
        detail::merge_positions(result, empty);

        REQUIRE(boost::equal(result, expected));
    }

    {
        auto result = intervals;
        std::vector<interval<int>> expected{
            {1, 4}, 9, {11, 61}, 81
        };
        std::vector<iterator> iters{
            result.begin(), result.begin() + 3, result.begin() + 4
        };
        detail::merge_positions(result, iters);
        REQUIRE(boost::equal(result, expected));
    }

    {
        auto result = intervals;
        std::vector<interval<int>> expected{
            1, {3, 4}, 9, {11, 55}, 57, {60, 81}
        };
        std::vector<iterator> iters{result.end() - 2};
        detail::merge_positions(result, iters);
        REQUIRE(boost::equal(result, expected));
    }

    {
        auto result = intervals;
        std::vector<interval<int>> expected{
            {1, 81}
        };
        std::vector<iterator> iters;
        for (auto i = result.begin(), e = result.end() - 1; i != e; ++i) {
            iters.push_back(i);
        }
        detail::merge_positions(result, iters);
        REQUIRE(boost::equal(result, expected));
    }

    {
        auto result = intervals;
        std::vector<interval<int>> expected{
            1, {3, 61}, 81
        };
        std::vector<iterator> iters;
        for (auto i = result.begin() + 1, e = result.end() - 2; i != e; ++i) {
            iters.push_back(i);
        }
        detail::merge_positions(result, iters);
        REQUIRE(boost::equal(result, expected));
    }
    {
        std::vector<interval<int>> result{3, 8, 10};
        std::vector<interval<int>> expected{{3, 10}};
        std::vector<iterator> iters{result.begin(), result.begin() + 1};
        detail::merge_positions(result, iters);
        REQUIRE(boost::equal(result, expected));
    }
}

TEST_CASE("set union", "[interval-set]") {
    std::vector<small_set> sets;

    SECTION("no loss") {
        sets.push_back({3, {5, 11}});
        sets.push_back({3, {4, 5}});
        sets.push_back({13});

        small_set expected{3, {4, 11}, 13};
        small_set result = small_set::set_union(sets);
        CHECK(boost::equal(result, expected));
    }

    SECTION("merge required") {
        sets.push_back({3, 4, {50, 99}});
        sets.push_back({{1, 20}});
        sets.push_back({{200, 220}, {1000, 2000}});

        small_set expected{{1, 99}, {200, 220}, {1000, 2000}};
        small_set result = small_set::set_union(sets);
        CHECK(boost::equal(result, expected));
    }

    SECTION("single result") {
        sets.push_back({{1, 10}, {21, 30}});
        sets.push_back({{1, 40}});
        sets.push_back({{11, 20}, 25, 30});
        sets.push_back({15, 20, {31, 40}});

        small_set expected{{1, 40}};
        small_set result = small_set::set_union(sets);
        CHECK(boost::equal(result, expected));
    }
}

TEST_CASE("set intersection", "[interval-set]") {
    std::vector<small_set> sets;

    SECTION("empty intersection") {
        sets.push_back({3, {5, 10}});
        sets.push_back({4, {5, 10}});
        sets.push_back({3, {11, 12}});

        small_set expected;
        small_set result = small_set::set_intersection(sets);
        CHECK(boost::equal(result, expected));
    }

    SECTION("single point") {
        sets.push_back({{5, 11}, 18, 19});
        sets.push_back({{11, 15}, 16, 17});

        small_set expected{11};
        small_set result = small_set::set_intersection(sets);
        CHECK(boost::equal(result, expected));
    }

    SECTION("multiple overlaps") {
        sets.push_back({{1, 10}, {20, 30}, 38});
        sets.push_back({{4, 9}, {21, 25}, {26, 30}});
        sets.push_back({{2, 10}, {20, 29}});
        sets.push_back({{1, 9}, {21, 30}, {99, 100}});

        small_set expected{{4, 9}, {21, 25}, {26, 29}};
        small_set result = small_set::set_intersection(sets);
        CHECK(boost::equal(result, expected));
    }
}

TEST_CASE("interval set assignment", "[interval-set]") {
    small_set s{1, 2, 3};

    std::initializer_list<interval<int>> i1{4, 5, 6};
    s.assign(i1.begin(), i1.end());
    REQUIRE(boost::equal(s, i1));

    std::initializer_list<interval<int>> i2{{4, 7}, 9, 10, 14};
    std::initializer_list<interval<int>> expected{{4, 7}, {9, 10}, 14};
    s.assign(i2.begin(), i2.end());
    REQUIRE(boost::equal(s, expected));
}

TEST_CASE("set display", "[interval-set]") {
    auto str = [&](auto&& set) {
        std::stringstream ss;
        ss << set;
        return ss.str();
    };

    REQUIRE(str(small_set({1, 3, 5})) == "{[1][3][5]}");
    REQUIRE(str(small_set({1, {2, 3}, 5})) == "{[1-3][5]}");
    REQUIRE(str(small_set({{1, 5}, 7})) == "{[1-5][7]}");
    REQUIRE(str(small_set({1, {3, 5}, {6, 9}})) == "{[1][3-9]}");
}
