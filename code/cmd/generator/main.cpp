#include "common/common.hpp"

#include "geodb/vector.hpp"
#include "geodb/irwi/base.hpp"

#include <boost/program_options.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <tpie/file_stream.h>

#include <iostream>
#include <random>

namespace po = boost::program_options;

using namespace geodb;

using std::cout;
using std::cerr;

static std::string output;
static u64 trajectory_units = 0;
static u32 trajectory_size = 0;
static u32 labels = 0;
static double highx = 1000;
static double highy = 1000;

static void parse_options(int argc, char** argv) {
    po::options_description options("Options");
    options.add_options()
            ("help,h", "Show this message.")
            ("output", po::value(&output)->required()->value_name("PATH"),
             "Path to the output file.")
            (",n", po::value(&trajectory_units)->required()->value_name("N"),
             "The number of trajectory units.")
            (",m", po::value(&trajectory_size)->value_name("M")->default_value(1000),
             "The (average) number of trajectory units per trajectory")
            (",l", po::value(&labels)->value_name("L")->default_value(1000),
             "The number of different labels.")
            (",x", po::value(&highx)->value_name("MAX"),
             "Maximum x value for start points.")
            (",y", po::value(&highy)->value_name("MAX"),
             "Maximum y value for start points.");

    po::variables_map vm;
    try {
        po::command_line_parser p(argc, argv);
        p.options(options);
        po::store(p.run(), vm);

        if (vm.count("help")) {
            fmt::print(cerr, "Usage: {0}\n"
                             "\n"
                             "Generates a file that contains N trajectory units\n"
                             "grouped into trajectories of (about) M units each.\n"
                             "The entries in the file can be inserted into a tree\n"
                             "using the \"loader\" command.\n"
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

using dist_t = std::uniform_real_distribution<double>;

static std::mt19937_64 rng{std::random_device{}()};

/// Returns a value in [0, 1).
static double get_random() {
    return dist_t{0, 1}(rng);
}

/// Returns a value in [low, high).
static double get_random(double low, double high) {
    geodb_assert(high >= low, "invalid bounds");
    return dist_t{low, high}(rng);
}

/// Returns the start point for a new random walk.
static vector3 point() {
    return vector3(get_random() * highx, get_random() * highy, get_random() * 100000);
}

/// Returns the next point in the random walk.
static vector3 point(const vector3& last) {
    return vector3(last.x() + get_random(-5, 5),
                   last.y() + get_random(-5, 5),
                   last.t() + get_random(5, 25)); // Always advance in time.
}

/// Returns a random label.
static label_type label() {
    return get_random() * labels;
}

/// Returns the next label in the random walk.
/// Changes to another random label with a probability of 20%.
static label_type label(label_type last) {
    if (get_random() < 0.2) {
        return label();
    }
    return last;
}

/// Generates a single trajectory as a random walk with `size` trajectory units.
static void generate_walk(u64 id, u32 size, tpie::file_stream<tree_entry>& out) {
    if (size == 0)
        return;

    vector3 p = point();
    label_type l = label();
    for (u32 index = 0; index < size; ++index) {
        vector3 q = point(p);
        tree_entry entry(id, index, trajectory_unit(p, q, l));
        out.write(entry);

        p = q;
        l = label(l);
    }
}

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        tpie::file_stream<tree_entry> out;
        out.open(output);
        out.truncate(0);

        u64 remaining = trajectory_units;
        trajectory_id_type id = 1;
        while (remaining > 0) {
            u32 size = get_random(trajectory_size * 0.5, trajectory_size * 1.5);
            if (size > remaining) {
                size = remaining;
            }

            generate_walk(id++, size, out);

            remaining -= size;
        }

        return 0;
    });
}



