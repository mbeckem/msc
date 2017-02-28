#include <catch.hpp>

#include "geodb/irwi/string_map.hpp"
#include "geodb/irwi/string_map_external.hpp"
#include "geodb/irwi/string_map_internal.hpp"

#include <boost/range/algorithm/equal.hpp>
#include <tpie/tempname.h>

using namespace geodb;

using internal = string_map_internal;
using external = string_map_external;

template<typename Func>
void test_internal_external(Func&& f) {
    {
        INFO("internal");
        string_map<internal> s;
        f(s);
    }
    {
        INFO("external");
        tpie::temp_file tmp;
        string_map<external> s(external(tmp.path()));
        f(s);
    }
}

TEST_CASE("test insertion", "[string-map]") {
    test_internal_external([](auto&& m) {
        REQUIRE(m.empty());
        REQUIRE(m.size() == 0);

        label_type id1 = m.insert("A");
        REQUIRE(id1 == 1);
        REQUIRE(m.size() == 1);
        REQUIRE(!m.empty());

        label_type id2 = m.insert("B");
        REQUIRE(id2 == 2);
        REQUIRE(m.size() == 2);
        REQUIRE(!m.empty());

        REQUIRE(m.label_name(id1) == "A");
        REQUIRE(m.label_name(id2) == "B");

        REQUIRE(m.label_id("A") == id1);
        REQUIRE(m.label_id("B") == id2);
    });
}

TEST_CASE("test iteration", "[string-map]") {
    test_internal_external([](auto&& m) {
        m.insert("C");
        m.insert("B");
        m.insert("D");
        m.insert("A");

        // Insertion order.
        std::vector<label_mapping> expected{
            {1, "C"}, {2, "B"}, {3, "D"}, {4, "A"}
        };
        REQUIRE(boost::range::equal(m, expected));
    });
}

TEST_CASE("test id or insert", "[string-map]") {
    test_internal_external([](auto&& m) {
        // key doesnt exist.
        label_type id1 = m.label_id_or_insert("asd");
        REQUIRE(id1 == 1);
        REQUIRE(m.label_id("asd") == id1);
        REQUIRE(m.label_id_or_insert("asd") == id1);
        REQUIRE(m.size() == 1);

        // key exists.
        label_type id2 = m.insert("123");
        REQUIRE(id2 == 2);
        REQUIRE(m.label_id_or_insert("123") == id2);
        REQUIRE(m.size() == 2);
    });
}

TEST_CASE("test external storage", "[string-map]") {
    tpie::temp_file tmp;
    {
        string_map<external> m(external(tmp.path()));
        m.insert("a");
        m.insert("b");
        m.insert("cde");
    }
    {
        string_map<external> m(external(tmp.path()));
        REQUIRE(m.size() == 3);
        REQUIRE(m.label_id("a") == 1);
        REQUIRE(m.label_id("b") == 2);
        REQUIRE(m.label_id("cde") == 3);

        REQUIRE(m.insert("X") == 4);
    }
}
