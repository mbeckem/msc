#include <catch.hpp>

#include "geodb/irwi/posting.hpp"
#include "geodb/irwi/postings_list.hpp"
#include "geodb/irwi/postings_list_internal.hpp"
#include "geodb/irwi/postings_list_external.hpp"

#include <boost/range/counting_range.hpp>
#include <tpie/tempname.h>

using namespace geodb;

// Number of intervals in each posting.
constexpr size_t Lambda = 3;

using internal = postings_list_internal;
using external = postings_list_external;

template<typename Func>
void test_internal_external(Func&& f) {

    {
        INFO("internal");
        postings_list<internal, Lambda> p;
        f(p);
    }

    {
        INFO("external");
        tpie::temp_file tmp;
        postings_list<external, Lambda> p(external(tmp.path()));
        f(p);
    }
}

const posting<Lambda> p0(0, 123, {3, 4, 5});
const posting<Lambda> p1(1, 33, {{3, 10}, 15, {18, 33}});
const posting<Lambda> p2(2, 5, {9});
const posting<Lambda> p3(3, 9, {9, 999});

TEST_CASE("postings list basic usage", "[postings-list]") {
    test_internal_external([](auto&& p) {
        REQUIRE(p.empty());
        p.append(p0);
        p.append(p1);

        REQUIRE(p.size() == 2);
        REQUIRE(p[0] == p0);
        REQUIRE(p[1] == p1);

        REQUIRE(*p.begin() == p0);
        p.set(p.begin(), p2);
        REQUIRE(p[0] == p2);

        p.append(p3);
        p.append(p0);
        REQUIRE(p.size() == 4);

        // Remove: Swap end to positoin and pop back
        p.remove(p.begin());
        REQUIRE(p.size() == 3);
        REQUIRE(p[0] == p0);
        REQUIRE(p[1] == p1);
        REQUIRE(p[2] == p3);
    });
}

TEST_CASE("iteration", "[postings-list]") {
    test_internal_external([](auto&& p) {
        REQUIRE(p.empty());

        p.append(p3);
        p.append(p0);
        p.append(p2);
        p.append(p1);

        std::vector<posting<Lambda>> expected{p3, p0, p2, p1};
        REQUIRE(boost::range::equal(p, expected));
    });
}

TEST_CASE("external storage", "[postings-list]") {
    tpie::temp_file tmp;

    const u32 count = 1234;
    {
        postings_list<external, Lambda> p(external(tmp.path()));
        for (u32 i = 0; i < count; ++i) {
            p.append(posting<Lambda>(i, count - i, {}));
        }
    }
    {
        postings_list<external, Lambda> p(external(tmp.path()));
        REQUIRE(p.size() == count);

        auto cmp = [&](auto&& posting, u32 i) {
            return posting.node() == i && posting.count() == count - i;
        };
        REQUIRE(boost::equal(p, boost::counting_range(u32(0), count), cmp));
    }
}
