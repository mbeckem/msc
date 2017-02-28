#include <catch.hpp>

#include "geodb/utility/raw_stream.hpp"

#include <tpie/tempname.h>

using namespace geodb;

TEST_CASE("open / close", "[raw-stream]") {
    tpie::temp_file temp;

    raw_stream stream;
    REQUIRE_THROWS(stream.open_readonly(temp.path()));
    REQUIRE_FALSE(stream.try_open(temp.path()));
    REQUIRE_FALSE(stream.is_open());

    stream.open_new(temp.path());
    REQUIRE(stream.is_open());
    REQUIRE(stream.path() == temp.path());

    stream.close();
    REQUIRE_FALSE(stream.is_open());
    REQUIRE(stream.path().empty());

    REQUIRE_NOTHROW(stream.open_readonly(temp.path()));
    REQUIRE(stream.is_open());
    REQUIRE(stream.path() == temp.path());
}

TEST_CASE("read / write", "[raw-stream]") {
    tpie::temp_file temp;

    raw_stream stream;
    stream.open_new(temp.path());

    int v1 = 1;
    double v2 = 3.5;
    size_t v3 = 123123;

    stream.write(v1);
    stream.write(v2);
    stream.write(v3);
    REQUIRE(stream.size() == (sizeof(v1) + sizeof(v2) + sizeof(v3)));

    stream.seek(0);
    stream.read(v1);
    stream.read(v2);
    stream.read(v3);
    REQUIRE(v1 == 1);
    REQUIRE(v2 == 3.5);
    REQUIRE(v3 == 123123);
}
