#include <catch.hpp>

#include "geodb/algorithm.hpp"
#include "geodb/parser.hpp"

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

using namespace  geodb;

template<typename Vector>
std::string vec_to_string(const Vector& v) {
    std::string str;
    str += "{";
    size_t count = 0;
    for (auto&& item : v) {
        if (count++) {
            str += ",";
        }
        str += item;
    }
    str += "}";
    return str;
}

TEST_CASE("split tokens", "[parser]") {
    struct test {
        std::string input;
        std::vector<std::string> expected;
    };

    test tests[]{
        {"Hello", {"Hello"}},
        {"", {}},
        {";;", {"", "", ""}},
        {"Hello;World; 123; !!!", {"Hello", "World", " 123", " !!!"}},
    };

    for (test& t : tests) {
        std::vector<std::string> got;
        for_each_token(t.input, ';', [&](auto range) {
            got.push_back(std::string(range.begin(), range.end()));
        });

        INFO("input: " << t.input);
        INFO("expected: " << vec_to_string(t.expected));
        INFO("got: " << vec_to_string(got));
        CHECK(boost::equal(t.expected, got));
    }
}

TEST_CASE("parse plt format", "[parser]") {
    std::string input =
        "Geolife trajectory\n"
        "WGS 84\n"
        "Altitude is in Feet\n"
        "Reserved 3\n"
        "0,2,255,My Track,0,0,2,8421376\n"
        "0\n"
        "39.984702,116.318417,0,492,39744.1201851852,2008-10-23,02:53:04\n"
        "39.984683,116.31845,0,492,39744.1202546296,2008-10-23,02:53:10\n"
        "39.984686,116.318417,0,492,39744.1203125,2008-10-23,02:53:15\n"
        "39.984688,116.318385,0,492,39744.1203703704,2008-10-23,02:53:20\n"
        "39.984655,116.318263,0,492,39744.1204282407,2008-10-23,02:53:25\n"
        "39.984611,116.318026,0,493,39744.1204861111,2008-10-23,02:53:30";
    std::stringstream in(input);
    std::vector<geolife_point> points;
    parse_geolife_points(in, points);
    std::cout << std::setprecision(16);
    for (const auto& p : points) {
        //std::cout << p << std::endl;
    }
}

TEST_CASE("parse labels format", "[parser]") {
    std::string input =
        "Start Time	End Time	Transportation Mode\n"
        "2008/04/30 13:39:34	2008/04/30 13:48:31	bus\n"
        "2008/04/30 13:48:32	2008/04/30 13:59:26	walk\n"
        "2008/05/01 04:08:03	2008/05/01 05:45:42	walk\n"
        "2008/05/01 07:58:51	2008/05/01 09:11:33	walk\n"
        "2008/05/01 11:09:46	2008/05/01 11:44:17	walk\n"
        "2008/05/02 00:28:38	2008/05/02 01:14:16	walk\n"
        "2008/05/02 02:42:20	2008/05/02 04:14:04	walk\n"
        "2008/05/02 05:31:48	2008/05/02 06:52:22	walk\n"
        "2008/05/02 06:52:23	2008/05/02 07:03:23	bus\n"
        "2008/05/02 07:03:24	2008/05/02 07:09:48	walk\n"
        "2008/05/02 07:09:42	2008/05/02 09:34:20	car";
    std::stringstream in(input);
    std::vector<geolife_activity> labels;
    parse_geolife_labels(in, labels);
    std::cout << std::setprecision(16);
    for (const auto& p : labels) {
        //std::cout << p << std::endl;
    }
}
