#include "common/common.hpp"

#include <boost/program_options.hpp>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <mapbox/variant.hpp>

#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/osrm.hpp>
#include <osrm/route_parameters.hpp>
#include <osrm/status.hpp>
#include <osrm/storage_config.hpp>

#include <sstream>
#include <iostream>
#include <vector>

using std::cout;
using std::cerr;

using namespace geodb;
namespace po = boost::program_options;

struct city {
    std::string name;  //
    vector2d position; // Latitude, Longitude
};

/// 100 largest cities in germany.
/// Source: http://www.tageo.com/index-e-gm-cities-DE.htm
static const std::vector<city> cities{
    city{"Berlin", {52.520,13.380}},
    city{"Hamburg", {53.550,10.000}},
    city{"Munchen", {48.140,11.580}},
    city{"Koln", {50.950,6.970}},
    city{"Frankfurt", {50.120,8.680}},
    city{"Dortmund", {51.510,7.480}},
    city{"Stuttgart", {48.790,9.190}},
    city{"Essen", {51.470,7.000}},
    city{"Dusseldorf", {51.240,6.790}},
    city{"Bremen", {53.080,8.810}},
    city{"Hannover", {52.400,9.730}},
    city{"Duisburg", {51.430,6.750}},
    city{"Nurnberg", {49.450,11.050}},
    city{"Leipzig", {51.350,12.400}},
    city{"Dresden", {51.050,13.740}},
    city{"Bochum", {51.480,7.200}},
    city{"Wuppertal", {51.260,7.180}},
    city{"Bielefeld", {52.030,8.530}},
    city{"Bonn", {50.730,7.100}},
    city{"Mannheim", {49.500,8.470}},
    city{"Karlsruhe", {49.000,8.400}},
    city{"Gelsenkirchen", {51.510,7.110}},
    city{"Wiesbaden", {50.080,8.230}},
    city{"Munster", {51.960,7.620}},
    city{"Monchengladbach", {51.200,6.420}},
    city{"Augsburg", {48.360,10.890}},
    city{"Chemnitz", {50.830,12.920}},
    city{"Aachen", {50.770,6.090}},
    city{"Braunschweig", {52.270,10.510}},
    city{"Krefeld", {51.330,6.550}},
    city{"Halle", {51.480,11.960}},
    city{"Kiel", {54.320,10.120}},
    city{"Magdeburg", {52.130,11.620}},
    city{"Oberhausen", {51.470,6.860}},
    city{"Lubeck", {53.870,10.660}},
    city{"Freiburg", {47.990,7.850}},
    city{"Hagen", {51.370,7.460}},
    city{"Erfurt", {50.990,11.030}},
    city{"Rostock", {54.090,12.100}},
    city{"Kassel", {51.320,9.480}},
    city{"Mainz", {50.000,8.260}},
    city{"Hamm", {51.670,7.800}},
    city{"Saarbrucken", {49.250,6.970}},
    city{"Herne", {51.540,7.210}},
    city{"Mulheim", {51.430,6.860}},
    city{"Osnabruck", {52.280,8.050}},
    city{"Solingen", {51.180,7.060}},
    city{"Ludwigshafen", {49.480,8.440}},
    city{"Leverkusen", {51.040,6.990}},
    city{"Oldenburg", {53.150,8.210}},
    city{"Neuss", {51.200,6.690}},
    city{"Heidelberg", {49.420,8.690}},
    city{"Paderborn", {51.720,8.740}},
    city{"Darmstadt", {49.870,8.640}},
    city{"Wurzburg", {49.800,9.940}},
    city{"Potsdam", {52.400,13.070}},
    city{"Regensburg", {49.020,12.110}},
    city{"Recklinghausen", {51.610,7.190}},
    city{"Gottingen", {51.530,9.920}},
    city{"Wolfsburg", {52.430,10.780}},
    city{"Heilbronn", {49.140,9.220}},
    city{"Bottrop", {51.530,6.930}},
    city{"Offenbach", {50.100,8.770}},
    city{"Ulm", {48.400,9.970}},
    city{"Bremerhaven", {53.550,8.580}},
    city{"Pforzheim", {48.890,8.690}},
    city{"Ingolstadt", {48.770,11.430}},
    city{"Remscheid", {51.180,7.190}},
    city{"Furth", {49.480,10.980}},
    city{"Reutlingen", {48.490,9.210}},
    city{"Salzgitter", {52.170,10.330}},
    city{"Koblenz", {50.350,7.600}},
    city{"Siegen", {50.870,8.010}},
    city{"Moers", {51.450,6.650}},
    city{"Gera", {50.880,12.080}},
    city{"Bergisch gladbach", {50.980,7.150}},
    city{"Hildesheim", {52.160,9.950}},
    city{"Witten", {51.440,7.340}},
    city{"Erlangen", {49.600,11.010}},
    city{"Cottbus", {51.770,14.330}},
    city{"Trier", {49.750,6.630}},
    city{"Zwickau", {50.720,12.500}},
    city{"Jena", {50.930,11.580}},
    city{"Kaiserslautern", {49.450,7.750}},
    city{"Iserlohn", {51.390,7.680}},
    city{"Schwerin", {53.630,11.400}},
    city{"Gutersloh", {51.910,8.370}},
    city{"Duren", {50.810,6.490}},
    city{"Ratingen", {51.300,6.850}},
    city{"Marl", {51.680,7.120}},
    city{"Lunen", {51.620,7.520}},
    city{"Esslingen", {48.740,9.320}},
    city{"Velbert", {51.350,7.040}},
    city{"Hanau", {50.140,8.910}},
    city{"Ludwigsburg", {48.900,9.190}},
    city{"Flensburg", {54.790,9.420}},
    city{"Wilhelmshaven", {53.540,8.110}},
    city{"Tubingen", {48.530,9.060}},
    city{"Minden", {52.290,8.900}},
    city{"Villingen-schwenningen", {48.070,8.450}},
};

static std::string map;
static std::string strings;
static std::string output;
static u64 entries;

static void parse_options(int argc, char** argv) {
    po::options_description options("Options");
    options.add_options()
            ("help,h", "Show this message.")
            ("map,m", po::value(&map)->required(),
             "The osrm map database on disk.")
            ("strings,s", po::value(&strings)->required(),
             "String database on disk.")
            ("output,o", po::value(&output)->required(),
             "The output file.")
            (",n", po::value(&entries)->value_name("N")->required(),
             "Stop when N entries have been generated.");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0} OPTION...\n"
                             "\n"
                             "Opens a preprocessed map database (created by the osrm tools)\n"
                             "and generates random trajectories between german cities.\n"
                             "Street names are used as labels, route segments become trajectory units.\n"
                             "Trajectory units are then written to disk as a tree entry file, ready to be"
                             "loaded into a IRWI Tree.\n"
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
}

/// Map a osrm::json::Value to a better json struct (which is actually usable).
static json map_json(const osrm::json::Value& v) {
    struct mapper {
        json operator()(const osrm::json::Object& o) const {
            json j = json::object();
            for (const auto& pair : o.values) {
                j[pair.first] = map_json(pair.second);
            }
            return j;
        }

        json operator()(const osrm::json::Array& a) const {
            json j = json::array();
            for (const auto& value : a.values) {
                j.push_back(map_json(value));
            }
            return j;
        }

        json operator()(const osrm::json::String& str) const {
            return str.value;
        }

        json operator()(const osrm::json::Number& num) const {
            return num.value;
        }

        json operator()(const osrm::json::True&) const {
            return true;
        }

        json operator()(const osrm::json::False&) const {
            return false;
        }

        json operator()(const osrm::json::Null&) const {
            return nullptr;
        }
    };

    return mapbox::util::apply_visitor(mapper(), v);
}

/// Generates routes between two cities.
struct route_generator {
    osrm::EngineConfig config;
    osrm::OSRM router;
    external_string_map& strings;

    route_generator(const std::string& db, external_string_map& strings)
        : config(get_config(db))
        , router(config)
        , strings(strings)
    {}

    /// Generates a list of trajectory units by computing a route between
    /// the two cities and taking the individual route segments.
    /// Long segments are split into short ones by simulating the beginning
    /// of a new segment about every 15 seconds.
    std::vector<trajectory_unit> generate(const city& a, const city& b) {
        osrm::RouteParameters params;

        // API has longitude and latitude reversed...
        params.coordinates.push_back({osrm::util::FloatLongitude{a.position.y()}, osrm::util::FloatLatitude{a.position.x()}});
        params.coordinates.push_back({osrm::util::FloatLongitude{b.position.y()}, osrm::util::FloatLatitude{b.position.x()}});
        params.annotations = false;
        params.steps = true;
        params.generate_hints = false;
        params.overview = osrm::RouteParameters::OverviewType::False;

        osrm::json::Object result;
        const auto status = router.Route(params, result);
        if (status != osrm::Status::Ok) {
            throw std::runtime_error(fmt::format("Failed to compute route: {}\n", map_json(result).dump(4)));
        }

        return get_route(a, b, map_json(result));
    }

private:
    /// Returns the name of the given route step.
    /// Prefers the ref, then the name and finally falls back to "N/A".
    std::string get_name(const json& step) {
        if (step.count("ref")) {
            std::string ref = step.at("ref");
            if (!ref.empty())
                return ref;
        }
        if (step.count("name")) {
            std::string name = step.at("name");
            if (!name.empty())
                return name;
        }
        return "N/A";
    }

    /// Transforms the route result obtained from osrm into a vector of trajectory units.
    std::vector<trajectory_unit> get_route(const city& a, const city& b, const json& result) {
        // The last step of the route should have this endpoint already.
        unused(b);

        // route data is at routes[0]->legs[0]->steps.
        const json& steps = result
                .at("routes").at(0)
                .at("legs").at(0)
                .at("steps");

        std::vector<trajectory_unit> units;

        double x = a.position.x();
        double y = a.position.y();
        time_type t = 0;

        for (const json& step : steps) {
            const json& location = step.at("maneuver").at("location");

            const double duration = step.at("duration"); // seconds
            const std::string name = get_name(step);

            const double nx = location.at(1);
            const double ny = location.at(0);
            const time_type dt = std::max(time_type(1), time_type(duration));

            trajectory_unit unit;
            unit.label = strings.label_id_or_insert(name);
            unit.start = vector3(x, y, t);
            unit.end = vector3(nx, ny, t + dt);

            if (dt >= 20) {
                split_segments(unit, 15, units);
            } else {
                units.push_back(unit);
            }

            x = nx;
            y = ny;
            t += dt;
        }

        return units;
    }

    /// Split the large unit into smaller units of duration `max_duration`,
    /// assuming linear movement.
    void split_segments(const trajectory_unit& unit,
                        time_type max_duration,
                        std::vector<trajectory_unit>& units) {
        const time_type total_duration = unit.end.t() - unit.start.t();
        const double dx = (unit.end.x() - unit.start.x()) / total_duration;
        const double dy = (unit.end.y() - unit.start.y()) / total_duration;

        vector3 current = unit.start;
        time_type remaining = total_duration;
        while (remaining) {
            const time_type duration = std::min(remaining, max_duration);

            vector3 next = current;
            next.x() += dx * duration;
            next.y() += dy * duration;
            next.t() += duration;

            units.emplace_back(current, next, unit.label);
            current = next;
            remaining -= duration;
        }
    }

private:
    static osrm::EngineConfig get_config(const std::string& db) {
        osrm::EngineConfig config;
        config.use_shared_memory = false;
        config.storage_config = { db };
        return config;
    }
};

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        external_string_map string_map({strings});

        route_generator gen(map, string_map);

        std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<size_t> dist(0, cities.size() - 1);
        for (int i = 0; i < 20; ++i) {
            size_t a = dist(rng);
            size_t b = dist(rng);
            if (a == b)
                continue;

            const auto units = gen.generate(cities[a], cities[b]);
            for (const trajectory_unit& unit : units) {
                std::cout << unit << std::endl;
            }

            break;
        }
        return 0;
    });
}
