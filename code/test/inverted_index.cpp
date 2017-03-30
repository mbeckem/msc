#include <catch.hpp>

#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/inverted_index_internal.hpp"
#include "geodb/irwi/inverted_index_external.hpp"
#include "geodb/utility/temp_dir.hpp"

using namespace geodb;

using internal = inverted_index_internal_storage;
using external = inverted_index_external<4096>;

static constexpr u32 Lambda = 16;

template<typename Func>
void test_internal_external(Func&& f) {
    {
        INFO("internal");
        inverted_index<internal, Lambda> i;
        f(i);
    }
    {
        tpie::temp_file tmp;
        block_collection<4096> blocks(tmp.path(), 32);

        INFO("external");
        temp_dir dir;
        inverted_index<external, Lambda> i(external(dir.path(), blocks));
        f(i);
    }
}

TEST_CASE("inverted file creation", "[inverted-file]") {
    test_internal_external([](auto&& index) {
        auto i1 = index.create(1);
        REQUIRE((i1 != index.end()));
        REQUIRE((i1->label() == 1));

        auto i2 = index.create(2);
        REQUIRE((i2 != index.end()));
        REQUIRE((i2->label() == 2));

        REQUIRE((i1 != i2));

        REQUIRE((std::distance(index.begin(), index.end()) == 2));
    });
}

TEST_CASE("find or create", "[inverted-file]") {
    test_internal_external([](auto&& index) {
        REQUIRE(index.find(123) == index.end());

        auto i1 = index.find_or_create(123);
        REQUIRE(i1->label() == 123);
        REQUIRE(index.find_or_create(123) == i1);

        auto i2 = index.create(124);
        REQUIRE(i2->label() == 124);
        REQUIRE(index.find_or_create(124) == i2);
    });
}

TEST_CASE("inverted file iteration", "[inverted-file]") {
    test_internal_external([](auto&& index) {
        index.create(1);
        index.create(2);
        index.create(7);
        index.create(42);

        std::set<label_type> seen;
        for (auto i : index) {
            INFO("label: " << i.label());
            REQUIRE((!seen.count(i.label())));

            seen.insert(i.label());
        }

        REQUIRE((seen.size() == 4));
        REQUIRE((seen.count(1)));
        REQUIRE((seen.count(2)));
        REQUIRE((seen.count(7)));
        REQUIRE((seen.count(42)));
    });
}
