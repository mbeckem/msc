#include "geodb/parser.hpp"

#include "geodb/algorithm.hpp"

#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/at_c.hpp>
#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/support/multi_pass.hpp>
#include <boost/spirit/home/support/iterators/line_pos_iterator.hpp>

#include <iterator>
#include <istream>
#include <string>

BOOST_FUSION_ADAPT_STRUCT(
    geodb::plt_point, latitude, longitude, time
)

BOOST_FUSION_ADAPT_STRUCT(
    geodb::activity, begin, end, name
)

namespace geodb {

namespace parser {
    namespace spirit = boost::spirit;
    namespace x3 = spirit::x3;
    namespace ascii = x3::ascii;

    using x3::rule;
    using x3::_val;
    using x3::_attr;

    using x3::char_;
    using x3::double_;
    using x3::int_;
    using x3::lit;
    using x3::eoi;
    using x3::eol;

    using x3::omit;
    using x3::expect;
    using x3::repeat;

    const auto line = *(char_ - eol);

    const auto line_list = [](auto&& list_elem) {
        return list_elem > *(eol > list_elem);
    };

    // Iterates over an input stream and supports backtracking when required.
    using istream_mpass = spirit::multi_pass<std::istreambuf_iterator<char>>;

    // A plt file contains a number of rows.
    // Each row defines one point in space and time.
    namespace plt {
        const auto sep = lit(',');

        const auto any = *(char_ - (sep | eol));

        rule<class date_time_tag, time::ptime> date_time = "plt date time";

        rule<class row_tag, plt_point> row = "plt row";

        rule<class file_tag, std::vector<plt_point>> file = "plt file";

        auto set_time = [](auto& ctx) {
            using boost::fusion::at_c;

            const auto& attr = _attr(ctx);
            gregorian::date date(at_c<0>(attr), at_c<1>(attr), at_c<2>(attr));
            time::time_duration tod(at_c<3>(attr), at_c<4>(attr), at_c<5>(attr));
            _val(ctx) = time::ptime(date, tod);
        };

        // Y/M/D,HH:MM:SS
        const auto date_time_def = (int_ > '-' > int_ > '-' > int_ > sep > int_ > ':' > int_ > ':' > int_)[set_time];

        const auto row_def =
                double_                 // latitude
                > sep > double_         // longitude
                > sep > omit[any]       // unused value, always 0
                > sep > omit[any]       // altitude
                > sep > omit[any]       // date as number of days (with fractional part)
                > sep > date_time;      // date, time

        const auto file_def =
            omit[repeat(6)[line > eol]]     // First 6 lines contain no useful information.
            > line_list(row)                // Each line contains a single point definition.
            > eoi;

        BOOST_SPIRIT_DEFINE(date_time, row, file)
    }

    // Every line in a labels.txt file contains a time interval
    // and a transportation mode.
    namespace labels {
        // Transportation mode: a simple string
        const auto mode = +ascii::alnum;

        rule<class date_time_tag, time::ptime> date_time = "date time";

        rule<class row_tag, activity> row = "label row";

        rule<class file_tag, std::vector<activity>> file = "labels file";

        auto set_time = [](auto& ctx) {
            using boost::fusion::at_c;

            const auto& attr = _attr(ctx);
            gregorian::date date(at_c<0>(attr), at_c<1>(attr), at_c<2>(attr));
            time::time_duration tod(at_c<3>(attr), at_c<4>(attr), at_c<5>(attr));
            _val(ctx) = time::ptime(date, tod);
        };

        // Year/Month/Day Hour:Minute:Second
        const auto date_time_def = (int_ > '/' > int_ > '/' > int_ > int_> ':' > int_ > ':' > int_)[set_time];

        // Each row contains a time interval and a transportation mode.
        const auto row_def = date_time > date_time > mode;

        // The first line is ignored (colum headers) and the rest must specify
        // the current transportation mode.
        const auto file_def = omit[line > eol] > line_list(row) > eoi;

        BOOST_SPIRIT_DEFINE(date_time, row, file)
    }
}

using pos_iter = parser::spirit::line_pos_iterator<parser::istream_mpass>;

template<typename Parser, typename Output>
void parse_helper(std::istream& in, Parser&& p, Output&& out, const char* type) {
    using namespace parser;

    pos_iter first{istream_mpass{in}};
    pos_iter last;

    bool ok = false;
    try {
        ok = x3::phrase_parse(first, last, p, x3::ascii::blank, out);
    } catch (const x3::expectation_failure<pos_iter>& fail) {
        throw parse_error("Expected " + fail.which() + " in line " + std::to_string(fail.where().position()));
    }
    if (!ok || first != last) {
        throw parse_error("Failed to parse " + std::string(type));
    }
}

void parse_plt(std::istream& in, std::vector<plt_point>& out) {
    using namespace parser;

    out.clear();
    parse_helper(in, expect[plt::file], out, "plt file");
}

void parse_labels(std::istream& in, std::vector<activity>& out) {
    using namespace parser;

    out.clear();
    parse_helper(in, expect[labels::file], out, "labels file");
}

} // namespace geodb
