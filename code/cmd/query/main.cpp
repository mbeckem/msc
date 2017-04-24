#include "common/common.hpp"

#include <boost/program_options.hpp>
#include <boost/fusion/adapted.hpp>
#include <boost/spirit/home/x3.hpp>
#include <fmt/ostream.h>

#include <iostream>
#include <string>
#include <sstream>

using namespace geodb;
namespace po = boost::program_options;

using std::cout;
using std::cerr;

struct raw_bounding_box {
    double x_min = 0;
    double x_max = 0;
    double y_min = 0;
    double y_max = 0;
    u32 t_min = 0;
    u32 t_max = 0;
};

struct raw_labels {
    std::vector<u32> list;
};

BOOST_FUSION_ADAPT_STRUCT(
    raw_bounding_box,
    x_min, x_max, y_min, y_max, t_min, t_max
);

namespace grammar {
    using namespace boost::spirit::x3;

    const auto bounding_box = lit('(')
            > double_ > lit(',')        // x min
            > double_ > lit(',')        // x max
            > double_ > lit(',')        // y min
            > double_ > lit(',')        // y max
            > uint32 > lit(',')         // t min
            > uint32                    // t max
            > lit(')');
}

static std::string tree_path;
static std::string results_path;
static std::string stats_path;
static std::vector<raw_bounding_box> rects;
static std::vector<raw_labels> labels;

// Parser for bounding boxes on the command line.
void validate(boost::any& v,
              const std::vector<std::string>& values,
              raw_bounding_box*, int)
{
    using namespace boost::program_options;
    namespace x3 = boost::spirit::x3;

    validators::check_first_occurrence(v);
    const std::string& s = validators::get_single_string(values);

    raw_bounding_box result;
    auto iter = s.begin();
    auto end = s.end();
    bool ok = x3::phrase_parse(iter, end,
                               grammar::bounding_box,
                               x3::ascii::space, result);

    if (!ok || iter != end) {
        throw validation_error(validation_error::invalid_option_value);
    }
    v = result;
}

void validate(boost::any& v,
              const std::vector<std::string>& values,
              raw_labels*, int)
{
    using namespace boost::program_options;
    namespace x3 = boost::spirit::x3;

    validators::check_first_occurrence(v);
    const std::string& s = validators::get_single_string(values);

    raw_labels result;
    auto iter = s.begin();
    auto end = s.end();
    bool ok = x3::phrase_parse(iter, end,
                               x3::eoi | (x3::uint32 % ','),
                               x3::ascii::space, result.list);

    if (!ok || iter != end) {
        throw validation_error(validation_error::invalid_option_value);
    }
    v = result;
}

static void parse_options(int argc, char** argv) {
    po::options_description options;
    options.add_options()
            ("help,h", "Show this message.")
            ("tree", po::value(&tree_path)->value_name("PATH")->required(),
             "The path to the tree on disk.")
            ("results", po::value(&results_path)->value_name("PATH"),
             "The path to the result file on disk (optional).")
            ("stats", po::value(&stats_path)->value_name("PATH"),
             "The path to the stats file on disk (optional).")
            ("rect,r", po::value(&rects)->value_name("RECT"),
             "Add a rectangle to the query."
             "The syntax is \"(xmin, xmax, ymin, ymax, tmin, tmax)\".")
            ("label,l", po::value(&labels)->value_name("LIST"),
             "Add a list of comma separated labels to the query. Use zero labels to express \"any\".")
            ;

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0} OPTION...\n"
                             "\n"
                             "Queries an IRWI-Tree, records the size of the result set,\n"
                             "and measures the time and number of IOs taken.\n"
                             "The result set can optionally be written to a file, but\n"
                             "it is not required for performance evaluation.\n"
                             "\n"
                             "{1}",
                       argv[0], options);
            throw exit_main(0);
        }

        po::notify(vm);
    } catch (const po::error& e) {
        fmt::print(cerr, "Failed to parse arguments: {}.\n", e.what());
        throw exit_main(1);
    }

    if (labels.size() != rects.size() || rects.empty()) {
        fmt::print(cerr, "Must specify the same number of rectangles and label lists.");
        throw exit_main(1);
    }
}

template<typename Container>
std::string container_to_string(const Container& c) {
    std::stringstream ss;
    ss << "{";

    size_t i = 0;
    for (const auto& e : c) {
        if (i++) {
            ss << ", ";
        }
        ss << e;
    }
    ss << "}";
    return ss.str();
}

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        fmt::print(cout, "Building the query.\n");
        sequenced_query query;
        for (size_t i = 0; i < rects.size(); ++i) {
            const raw_bounding_box& rbb = rects.at(i);
            const raw_labels& rlabels = labels.at(i);

            vector3 min(rbb.x_min, rbb.y_min, rbb.t_min);
            vector3 max(rbb.x_max, rbb.y_max, rbb.t_max);
            if (!vector3::less_eq(min, max)) {
                fmt::print(cerr, "Invalid bounding box. Minimum coordinates must be <= maximum coordinates.");
                throw exit_main(1);
            }

            simple_query q;
            q.rect = bounding_box(min, max);
            q.labels.insert(rlabels.list.begin(), rlabels.list.end());
            query.queries.push_back(q);

            fmt::print("Simple query #{}: {}, {}.\n", i + 1, q.rect, container_to_string(q.labels));
        }
        fmt::print(cout, "\n");


        fmt::print(cout, "Opening tree at \"{}\".\n", tree_path);
        external_tree tree{external_storage(tree_path)};
        fmt::print(cout, "Tree contains {} entries.\n", tree.size());
        fmt::print(cout, "\n");

        fmt::print(cout, "Running the query.\n");
        std::vector<trajectory_match> result;
        const measure_t stats = measure_call([&]{
            result = tree.find(query);
        });

        u64 units = 0;
        for (const auto& match: result) {
            units += match.units.size();
        }
        fmt::print("Found {} trajectories that satisfy the query with a total of {} matching units.\n", result.size(), units);

        fmt::print("\n"
                   "Blocks read: {}\n"
                   "Blocks written: {}\n"
                   "Blocks total: {}\n"
                   "Seconds: {}\n",
                   stats.read_io, stats.write_io, stats.total_io, stats.duration);

        if (!stats_path.empty()) {
            write_json(stats_path, stats);
        }

        if (!results_path.empty()) {
            std::ofstream out(results_path);
            fmt::print(out, "Found {} matching trajectories.\n", result.size());
            for (const trajectory_match& m : result) {
                fmt::print(out, "\n");
                fmt::print(out, "Trajectory {} ({} matching units):\n", m.id, m.units.size());
                for (const unit_match& u : m.units) {
                    fmt::print(out, "- Unit #{}: {}\n", u.index, u.unit);
                }
            }
        }

        return 0;
    });
}

