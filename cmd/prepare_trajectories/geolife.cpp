#include "geolife.hpp"

#include "geodb/filesystem.hpp"
#include "geodb/parser.hpp"
#include "geodb/tpie_main.hpp"

#include <boost/optional.hpp>
#include <fmt/ostream.h>

#include <iostream>
#include <iterator>

using std::cout;
using std::cerr;

using namespace geodb;

static const geodb::time::ptime epoch(geodb::gregorian::date(1970, 1, 1));

static geodb::time_type seconds(const geodb::time::ptime& time) {
    if (time < epoch)
        throw std::invalid_argument("invalid time (before epoch)");
    return (time - epoch).total_seconds();
}

namespace {

struct activity {
    geodb::time::ptime begin, end;
    geodb::label_type label;
};

struct geolife_parser {
    fs::path path;
    labels_map& labels;
    trajectory_id_type next_id = 1;

    geolife_parser(const fs::path& path, labels_map& labels)
        : path(path), labels(labels) {}

    std::vector<point_trajectory> parse() {
        std::vector<point_trajectory> result;

        if (!fs::exists(path)) {
            fmt::print(cerr, "Input directory does not exist: {}.\n", path);
            throw exit_main(1);
        }
        if (!fs::is_directory(path)) {
            fmt::print(cerr, "Input file is no directory: {}.\n", path);
            throw exit_main(1);
        }

        for (const fs::directory_entry& e : fs::directory_iterator(path))  {
            fs::path child = e.path();
            if (!fs::is_directory(child)) {
                continue;
            }

            fs::path labels_path = child / "labels.txt";
            if (!fs::exists(labels_path)) {
                // Not every trajectory set has associated labels.
                fmt::print(cout, "Skipping trajectories without labels: {}.\n", child);
                continue;
            }

            std::vector<activity> activities = parse_activities(labels_path);
            std::vector<point_trajectory> trajectories = parse_trajectories(child / "Trajectory", activities);
            result.insert(result.end(), std::make_move_iterator(trajectories.begin()), std::make_move_iterator(trajectories.end()));
        }

        return result;
    }

    std::vector<activity> parse_activities(const fs::path& path) {
        std::vector<geolife_activity> list;
        try {
            fs::ifstream in(path);
            parse_geolife_labels(in, list);
        } catch (const parse_error& e) {
            fmt::print(cerr, "Failed to parse {}: {}\n", path, e.what());
            throw exit_main(1);
        }

        std::vector<activity> result;
        result.resize(list.size());
        std::transform(list.begin(), list.end(), result.begin(), [&](const geolife_activity& a) {
            return activity{ a.begin, a.end, labels.label_id_or_insert(a.name) };
        });

        if (!std::is_sorted(result.begin(), result.end(), [&](const activity& a, const activity& b) { return a.begin < b.begin; })) {
            fmt::print(cerr, "Labels are not sorted by time: {}\n", path);
            throw exit_main(1);
        }
        return result;
    }

    std::vector<point_trajectory> parse_trajectories(const fs::path& path, const std::vector<activity>& activities) {
        std::string prefix = fs::basename(path);

        std::vector<point_trajectory> result;
        for (const fs::directory_entry& e :  fs::directory_iterator(path)) {
            parse_subtrajectories(prefix, e.path(), activities, result);
        }
        return result;
    }

    // FIXME: 1 trajectory per file
    void parse_subtrajectories(const std::string& prefix, const fs::path& path, const std::vector<activity>& activities, std::vector<point_trajectory>& result) {
        std::vector<geolife_point> list;
        try {
            fs::ifstream in(path);
            parse_geolife_points(in, list);
        } catch (const parse_error& e) {
            fmt::print(cerr, "Failed to parse {}: {}\n", path, e.what());
            throw exit_main(1);
        }

        auto a_pos = activities.begin();
        auto a_end = activities.end();
        auto l_pos = list.begin();
        auto l_end = list.end();

        int count = 0;
        while (a_pos != a_end && l_pos != l_end) {
            std::vector<trajectory_element> elems;
            l_pos = std::find_if(l_pos, l_end, [&](const geolife_point& gp) {
                return gp.time >= a_pos->begin;
            });

            for (; l_pos != l_end; ++l_pos) {
                while (l_pos->time > a_pos->end && a_pos != a_end) {
                    ++a_pos;
                }
                if (a_pos == a_end || l_pos->time < a_pos->begin) {
                    // Either no more activities or this point is not in range.
                    break;
                }

                // l_pos->time <= a_pos->end && l_pos_time >= a->pos->begin,
                // i.e. point is in range for activity.
                elems.push_back(trajectory_element(point(l_pos->latitude, l_pos->longitude, seconds(l_pos->time)),
                                                   a_pos->label));
            }

            if (elems.size() >= 2) {
                ++count;
                result.push_back(point_trajectory(next_id++, prefix + "/" + fs::basename(path), std::move(elems)));
            }
        }
        if (count == 0) {
            fmt::print(cout, "Trajectory {} contains no labelled parts, skipping...\n", path);
        }
    }
};

} // namespace

std::vector<point_trajectory> parse_geolife(const fs::path& path, labels_map& labels) {
    geolife_parser p(path, labels);
    return p.parse();
}
