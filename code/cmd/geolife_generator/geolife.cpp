#include "geolife.hpp"

#include "common/common.hpp"
#include "geodb/filesystem.hpp"
#include "geodb/parser.hpp"

#include <boost/optional.hpp>
#include <fmt/ostream.h>
#include <tpie/fractional_progress.h>
#include <tpie/progress_indicator_subindicator.h>

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
    external_string_map& labels;
    tpie::file_stream<tree_entry>& out;
    std::ostream& log;
    tpie::progress_indicator_base& progress;

    trajectory_id_type next_id = 1;

    geolife_parser(const fs::path& path, external_string_map& labels,
                   tpie::file_stream<tree_entry>& out,
                   std::ostream& log,
                   tpie::progress_indicator_base& progress)
        : path(path)
        , labels(labels)
        , out(out)
        , log(log)
        , progress(progress)
    {}

    void read() {
        if (!fs::exists(path)) {
            fmt::print(cerr, "Input directory does not exist: {}.\n", path);
            throw exit_main(1);
        }
        if (!fs::is_directory(path)) {
            fmt::print(cerr, "Input file is no directory: {}.\n", path);
            throw exit_main(1);
        }

        int directories = std::count_if(fs::directory_iterator(path), fs::directory_iterator(),
            [](const fs::directory_entry& e) { return e.status().type() == fs::directory_file; }
        );
        int current = 0;

        progress.init(directories);
        for (const fs::directory_entry& e : fs::directory_iterator(path)) {
            fs::path child = e.path();
            if (!fs::is_directory(child)) {
                continue;
            }

            ++current;
            fs::path labels_path = child / "labels.txt";
            if (!fs::exists(labels_path)) {
                // Not every trajectory set has associated labels.
                progress.step();
                continue;
            }

            std::string title = fmt::format("Subdirectory {} of {}", current, directories);
            tpie::progress_indicator_subindicator sub(&progress, 1, title.c_str());
            std::vector<activity> activities = parse_activities(labels_path);
            parse_trajectories(child / "Trajectory", activities, sub);
        }
        progress.done();
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

    void parse_trajectories(const fs::path& path, const std::vector<activity>& activities,
                            tpie::progress_indicator_base& progress)
    {
        int files = std::count_if(fs::directory_iterator(path), fs::directory_iterator(),
            [](const fs::directory_entry& entry) { return fs::is_regular_file(entry.status()); }
        );
        int current = 0;

        progress.init(files);
        for (const fs::directory_entry& e : fs::directory_iterator(path)) {
            if (!fs::is_regular_file(e.status())) {
                continue;
            }

            std::string title = fmt::format("File {} of {}", ++current, files);
            progress.push_breadcrumb(title.c_str(), tpie::IMPORTANCE_MINOR);
            progress.refresh();

            parse_trajectory_units(e.path(), activities);

            progress.pop_breadcrumb();
            progress.step(1);
        }
        progress.done();
    }

    void parse_trajectory_units(const fs::path& path, const std::vector<activity>& activities) {
        std::vector<geolife_point> list;
        try {
            fs::ifstream in(path);
            parse_geolife_points(in, list);
        } catch (const parse_error& e) {
            fmt::print(cerr, "Failed to parse {}: {}\n", path, e.what());
            throw exit_main(1);
        }

        const trajectory_id_type id = next_id++;
        fmt::print(log, "Trajectory #{}: {}\n", id, path.string());

        auto a_pos = activities.begin();
        auto a_end = activities.end();
        auto l_pos = list.begin();
        auto l_end = list.end();

        struct point {
            vector3 location;
            label_type label;
        };

        std::vector<point> points;
        u32 count = 0;
        while (a_pos != a_end && l_pos != l_end) {
            // Find the first point that has starts after the next activity.
            l_pos = std::find_if(l_pos, l_end, [&](const geolife_point& gp) {
                return gp.time >= a_pos->begin;
            });

            // Iterate over all points until one does not have an associated activity.
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
                points.push_back({vector3(l_pos->latitude, l_pos->longitude, seconds(l_pos->time)),
                                  a_pos->label});
            }

            // Connect all adjacent points to trajectory units.
            for_each_adjacent(points, [&](const point& a, const point& b) {
                tree_entry entry;
                entry.trajectory_id = id;
                entry.unit_index = count++;
                entry.unit = trajectory_unit{a.location, b.location, b.label};
                out.write(entry);
            });
            points.clear();
        }
    }
};

} // namespace

void parse_geolife(const fs::path& path, external_string_map& labels,
                   tpie::file_stream<tree_entry>& out,
                   std::ostream& log,
                   tpie::progress_indicator_base& progress) {
    geolife_parser p(path, labels, out, log, progress);
    p.read();
}
