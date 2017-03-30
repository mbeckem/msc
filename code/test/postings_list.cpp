#include <catch.hpp>

#include "geodb/irwi/posting.hpp"
#include "geodb/irwi/postings_list.hpp"
#include "geodb/irwi/postings_list_blocks.hpp"
#include "geodb/irwi/postings_list_internal.hpp"
#include "geodb/irwi/postings_list_external.hpp"

#include <boost/range/counting_range.hpp>
#include <tpie/tempname.h>

using namespace geodb;

// Number of intervals in each posting.
constexpr size_t Lambda = 3;

using internal = postings_list_internal;
using external = postings_list_external;
using blocks = postings_list_blocks<512>;

template<typename Func>
void perform_tests(Func&& f) {

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

    {
        INFO("blocks");

        tpie::temp_file tmp;
        block_collection<512> collection(tmp.path(), 4);
        postings_list<blocks, Lambda> p(blocks(collection, collection.get_free_block(), true));
        f(p);
    }
}

using posting_t = posting<Lambda>;

const posting_t p0(0, 123, {3, 4, 5});
const posting_t p1(1, 33, {3, 15, 18});
const posting_t p2(2, 5, {9});
const posting_t p3(3, 9, {9, 999});

bool equals(auto& list, std::initializer_list<posting_t> values) {
    return std::equal(list.begin(), list.end(), values.begin(), values.end());
}

TEST_CASE("postings list basic usage", "[postings-list]") {
    perform_tests([](auto&& p) {
        REQUIRE(p.empty());
        p.append(p0);
        p.append(p1);

        REQUIRE(p.size() == 2);
        REQUIRE(equals(p, {p0, p1}));

        REQUIRE(*p.begin() == p0);
        p.set(p.begin(), p2);
        REQUIRE(equals(p, {p2, p1}));

        p.append(p3);
        p.append(p0);
        REQUIRE(p.size() == 4);

        // Remove: Swap end to position and pop back
        p.remove(p.begin());
        REQUIRE(p.size() == 3);
        REQUIRE(equals(p, {p0, p1, p3}));
    });
}

TEST_CASE("postings list replace element", "[postings-list]") {
    perform_tests([](auto&& p) {
        p.append(p0);
        p.append(p1);
        p.append(p2);
        REQUIRE(p.size() == 3);

        p.remove(std::prev(p.end()));
        REQUIRE(p.size() == 2);
        REQUIRE(equals(p, {p0, p1}));

        p.remove(p.begin());
        REQUIRE(p.size() == 1);
        REQUIRE(equals(p, {p1}));

        p.remove(p.begin());
        REQUIRE(p.empty());
    });
}

TEST_CASE("iteration", "[postings-list]") {
    perform_tests([](auto&& p) {
        REQUIRE(p.empty());

        p.append(p3);
        p.append(p0);
        p.append(p2);
        p.append(p1);

        std::vector<posting_t> expected{p3, p0, p2, p1};
        REQUIRE(boost::range::equal(p, expected));
    });
}

TEST_CASE("large test", "[postings-list]") {
    perform_tests([](auto&& p) {
        std::vector<posting_t> data;
        for (u32 i = 0; i < 1000; ++i) {
            data.push_back(posting_t(i, i * 2, {i}));
        }

        for (auto& posting : data)
            p.append(posting);

        REQUIRE(std::equal(p.begin(), p.end(), data.begin(), data.end()));

        std::swap(data[500], data.back());
        data.pop_back();

        p.remove(std::next(p.begin(), 500));
        REQUIRE(std::equal(p.begin(), p.end(), data.begin(), data.end()));
    });
}

TEST_CASE("external storage", "[postings-list]") {
    tpie::temp_file tmp;

    const u32 count = 1234;
    {
        postings_list<external, Lambda> p(external(tmp.path()));
        for (u32 i = 0; i < count; ++i) {
            p.append(posting_t(i, count - i, {}));
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

TEST_CASE("block storage", "[postings-list]") {
    tpie::temp_file tmp;

    std::vector<posting_t> data;
    for (u32 i = 0; i < 1000; ++i) {
        data.push_back(posting_t(i, i * 2, {i}));
    }

    block_handle<512> base;
    {
        block_collection<512> collection(tmp.path(), 4);
        for (int i = 0; i < 12; ++i)
            collection.get_free_block(); // Throws some blocks away.
        base = collection.get_free_block();

        postings_list<blocks, Lambda> p(blocks(collection, base, true));

        for (auto& posting : data) {
            p.append(posting);
        }
    }
    {
        block_collection<512> collection(tmp.path(), 4);
        postings_list<blocks, Lambda> p(blocks(collection, base, false));

        REQUIRE(std::equal(p.begin(), p.end(), data.begin(), data.end()));
    }
}
