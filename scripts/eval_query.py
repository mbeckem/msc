#!/usr/bin/env python3

import json
import os
import subprocess
import random

import common
from common import OUTPUT_PATH, RESULT_PATH, SCRIPTS_PATH, TMP_PATH, QUERY, STATS
from common import compile
from lib.prettytable import PrettyTable


# Maps the keys of the dictionary d using the given function.
# Make sure that keys are still unique!
def map_keys(d, func):
    return {func(key): value for key, value in d.items()}


# Returns a closure that looks up the key in a dict loaded
# from the given path.
def load_strings(path):
    if not path.exists():
        raise RuntimeError("path does not exist: {}".format(path))

    with path.open("r") as file:
        strings = json.load(file)

    def lookup(key):
        return strings[key]
    return lookup


geolife_label = load_strings(OUTPUT_PATH / "geolife.strings.json")
osm_label = load_strings(OUTPUT_PATH / "osm.strings.json")


class BoundingBox:

    def __init__(self, min, max):
        self.min = min
        self.max = max

    def with_time(self, min_t, max_t):
        return BoundingBox((self.min[0], self.min[1], min_t),
                           (self.max[0], self.max[1], max_t))


class SimpleQuery:

    def __init__(self, mbb, labels):
        self.mbb = mbb
        self.labels = labels


# A single query is a list of (bounding box, labels) tuples.
# All trajectories (and their units) that match all parts of the query
# will be returned.
class Query:

    def __init__(self, name, *queries):
        self.name = name
        self.queries = list(queries)

    def count():
        return len(self.queries)


# Constructs a set set queries for the geolife dataset.
def get_geolife_queries():
    # mbb for the entire dataset.
    all = BoundingBox((18, -180, 0), (400, 180, 2**32 - 1))
    label = geolife_label

    large_queries = [
        # ~ 800k units
        SimpleQuery(BoundingBox((39.3, 116.2, 1213612909),
                                (40.04, 117.1, 1251516674)), [label("bus")]),
        # ~ 400k units
        SimpleQuery(BoundingBox((25, 115.9, 1176512188),
                                (40.3, 119.5, 1325344687)), [label("car")]),
        # ~ 900k units
        SimpleQuery(BoundingBox((38, 100, 120859000),
                                (40, 124.4, 1287794455)), [label("walk")]),
        # ~ 400k units
        SimpleQuery(BoundingBox((39.94, 115.9, 1216599837),
                                (40.1, 116.35, 1324606738)), [label("bike")]),
        # ~ 500k units
        SimpleQuery(all, [label("subway"), label("taxi")]),
    ]

    small_queries = [
        # ~ 14k units (labels are very rare, this is the logical OR of the labels).
        SimpleQuery(all, [
            label("boat"), label("run"),
            label("airplane"), label("motorcycle")
        ]),

        # ~ 4k
        SimpleQuery(all.with_time(1250805600, 1250892000), []),

        # ~ 6k
        SimpleQuery(BoundingBox((34.19103, 108.70539, 1200413626),
                                (34.24278, 108.96151, 1222508338)), []),

        # ~ 20k
        SimpleQuery(BoundingBox((39.907, 116.334, 1216459365),
                                (40.02, 116.558, 1216724611)), []),

        # ~ 19k
        SimpleQuery(BoundingBox((39.94, 116.24, 1301914488),
                                (39.99, 119.79, 1316918842)), [label("subway")]),

        # ~ 6k
        SimpleQuery(BoundingBox((39.9717, 116.415, 1220007200),
                                (39.9743, 116.4199, 1228867298)), [label("bike")]),

        # ~ 12k
        SimpleQuery(BoundingBox((39.910, 116.012, 1239502600),
                                (39.936, 116.327, 1317827900)), [label("walk")]),
    ]

    sequenced_queries = [
        [
            # ~ 300k
            SimpleQuery(all, [label("walk")]),
            SimpleQuery(all, [label("train")]),
        ],
        [
            # ~ 64k
            SimpleQuery(BoundingBox((21, -180, 1214900831),
                                    (54, 180, 1847573841)), [label("car"), label("train")]),
            SimpleQuery(BoundingBox((21, -180, 1237573841),
                                    (54, 180, 2000000000)), [label("walk"), label("bike")])
        ],
        [
            # ~ 160k
            SimpleQuery(all, [label("walk")]),
            SimpleQuery(all, [label("bus")]),
            SimpleQuery(all, [label("bike")]),
        ],
        [
            # ~ 190k
            SimpleQuery(all, [label("bike")]),
            SimpleQuery(all, [label("bus")]),
            SimpleQuery(all, [label("walk")]),
        ],
        [
            # ~ 240k
            SimpleQuery(all, [label("taxi"), label("bus"),
                              label("walk"), label("subway"),
                              label("train")]),
            SimpleQuery(all, [label("car")]),
            SimpleQuery(all, [label("taxi"), label("bus"),
                              label("walk"), label("subway"),
                              label("train")]),
        ],
        [
            # ~ 44k
            SimpleQuery(BoundingBox((18, 75, 1180000000),
                                    (50, 125, 1250000000)), [label("bus"), label("walk")]),
            SimpleQuery(all, [label("bike")]),
            SimpleQuery(BoundingBox((18, 75, 1180000000),
                                    (50, 125, 1250000000)), [label("walk")]),
            SimpleQuery(BoundingBox((18, 75, 1180000000),
                                    (50, 125, 1250000000)), [label("bike")]),
        ],
    ]

    # Queries that return a large result set (400k - 900k units of ~5.4m).
    large_results = [Query("GEOLIFE-LARGE-{}".format(i), sq)
                     for i, sq in enumerate(large_queries)]

    # Queries that select only few units.
    small_results = [Query("GEOLIFE-SMALL-{}".format(i), sq)
                     for i, sq in enumerate(small_queries)]

    # Sequenced query, i.e. multiple simple queries that have to be satisfied.
    sequenced = [Query("GEOLIFE-SEQUENCED-{}".format(i), *sq)
                 for i, sq in enumerate(sequenced_queries)]

    return {"large": large_results, "small": small_results, "sequenced": sequenced}


# Constructs a set of queries for the osm dataset.
def get_osm_queries():
    label = osm_label

    # Bounding box for all entries in the osm dataset.
    all = BoundingBox((47, 6, 0), (55, 15, 35000))

    large_queries = [
        # ~ 280k (Berlin, Hamburg)
        SimpleQuery(BoundingBox((52.366, 9.654, 0),
                                (53.666, 13.681, 35000)), []),
        # ~ 370k
        SimpleQuery(all, [label("A 1")]),

        # ~ 183k
        SimpleQuery(BoundingBox((52.366, 9.654, 0),
                                (53.666, 13.681, 35000)),
                    [label("A 1"), label("A 2"), label("A 7")]),

        # ~ 570k.
        SimpleQuery(all, [label("A 9"), label("A 3")]),

        # ~ 900k (Süddeutschland).
        SimpleQuery(BoundingBox((47.334, 5.273, 0),
                                (49.848, 15.447, 35000)), []),

        # ~ 430k
        SimpleQuery(BoundingBox((48.5, 6.1, 15000),
                                (52.5, 13.8, 35000)), []),

        #  ~ 430k (Ruhrgebiet)
        SimpleQuery(BoundingBox((50.646, 6.311, 0),
                                (51.635, 7.810, 5000)), [])
    ]

    small_queries = [
        # ~ 16k
        SimpleQuery(all, [label("B 8"), label("B 170"),
                          label("A 70"), label("A 565")]),

        # ~ 3k (Berlin Innenstadt)
        SimpleQuery(BoundingBox((52.493, 13.351, 0),
                                (52.545, 13.435, 35000)), []),

        # ~ 10k (Münsterland)
        SimpleQuery(BoundingBox((51.791, 6.427, 2000),
                                (52.237, 8.322, 4000)), []),

        # ~ 16k
        SimpleQuery(all.with_time(4500, 5000), [label("A 1")]),

        # ~ 9k (Theaterstraße Hannover).
        SimpleQuery(BoundingBox((48.68, 8.78, 0),
                                (48.88, 9.06, 10000)), []),
        # ~ 14k
        SimpleQuery(BoundingBox((47.334, 5.273, 0),
                                (49.848, 15.447, 2000)), [label("A 8")]),

        #  ~ 10k (Ruhrgebiet, A1)
        SimpleQuery(BoundingBox((50.646, 6.311, 0),
                                (51.635, 7.810, 5000)), [label("A1")])
    ]

    sequenced_queries = [
        # Münster -> A1
        [
            SimpleQuery(BoundingBox((51.927, 7.579, 0),
                                    (51.973, 7.690, 35000)), []),
            SimpleQuery(all, [label("A 1")])
        ],

        # A2, A7
        [
            SimpleQuery(all, [label("A 2")]),
            SimpleQuery(all, [label("A 7")]),
        ],

        # Route Berlin -> Hamburg
        [
            SimpleQuery(BoundingBox((52.493, 13.351, 0),
                                    (52.545, 13.435, 35000)), []),
            SimpleQuery(all, []),
            SimpleQuery(BoundingBox((53.422, 9.713, 0),
                                    (53.697, 10.26, 35000)), [])
        ],

        # Berlin -> Süddeutschland, über A9 und dann A6, A3
        [
            SimpleQuery(BoundingBox((52.493, 13.351, 0),
                                    (52.545, 13.435, 35000)), []),
            SimpleQuery(all, [label("A 9")]),
            SimpleQuery(BoundingBox((47.334, 5.273, 0),
                                    (49.848, 15.447, 35000)), [label("A 3"), label("A 6")]),
        ],

        # München -> Autobahn
        [
            SimpleQuery(BoundingBox((48.027, 11.371, 0),
                                    (48.237, 11.783, 35000)), []),
            SimpleQuery(all, [label("A 8"), label("A 9")]),
            SimpleQuery(all, [label("A 3"), label("A 4"), label("A 1")])

        ],

        # Berlin -> A1 -> Ruhgebiet.
        [
            SimpleQuery(BoundingBox((52.493, 13.351, 0),
                                    (52.545, 13.435, 35000)), []),
            SimpleQuery(all, [label("A 1")]),
            SimpleQuery(BoundingBox((50.646, 6.311, 0),
                                    (51.635, 7.810, 5000)), [])
        ],

        # München -> Autobahn -> Kiel
        [
            SimpleQuery(BoundingBox((48.027, 11.371, 0),
                                    (48.237, 11.783, 35000)), []),
            SimpleQuery(all, [label("A 8"), label("A 9")]),
            SimpleQuery(all, [label("A 3"), label("A 4"), label("A 1")]),
            SimpleQuery(BoundingBox((54.275, 10.009, 10000),
                                    (54.386, 10.242, 35000)), [])
        ],

        # Raum München -> Raum Frankfurt -> Düsseldorf
        [
            SimpleQuery(BoundingBox((47.63, 10.711, 0),
                                    (48.63, 12.42, 20000)), []),
            SimpleQuery(BoundingBox((49.51, 7.58, 5000),
                                    (50.67, 9.73, 20000)), []),
            SimpleQuery(BoundingBox((50.958, 6.317, 5000),
                                    (51.457, 7.471, 25000)), []),
            SimpleQuery(all, [label("A 1")]),
        ],
    ]

    large_results = [Query("OSM-LARGE-{}".format(i), sq)
                     for i, sq in enumerate(large_queries)]
    small_results = [Query("OSM-SMALL-{}".format(i), sq)
                     for i, sq in enumerate(small_queries)]
    sequenced = [Query("OSM-SEQUENCED-{}".format(i), *sq)
                 for i, sq in enumerate(sequenced_queries)]
    return {"large": large_results, "small": small_results, "sequenced": sequenced}


def get_random_walk_queries():
    # Bounding box for all entries in the osm dataset.
    all = BoundingBox((-1000, -1000, 0), (2000, 2000, 200000))

    rng = random.Random()
    rng.seed(1234567)

    # Around 1000 results each
    small_queries = []
    for i in range(20):
        xmin = rng.uniform(0, 1000)
        ymin = rng.uniform(0, 1000)
        tmin = rng.randint(5000, 50000)
        xmax = xmin + rng.uniform(100, 500)
        ymax = ymin + rng.uniform(100, 500)
        tmax = tmin + rng.randint(5000, 10000)
        label = rng.randint(0, 99)
        small_queries.append(SimpleQuery(BoundingBox((xmin, ymin, tmin),
                                                     (xmax, ymax, tmax)),
                                         [label]))

    # Up to 300k results each
    large_queries = []
    for i in range(20):
        xmin = rng.uniform(0, 1000)
        ymin = rng.uniform(0, 1000)
        tmin = rng.randint(5000, 10000)
        xmax = xmin + rng.uniform(400, 600)
        ymax = ymin + rng.uniform(400, 600)
        tmax = tmin + 40000
        labels = []
        for i in range(rng.randint(30, 60)):
            labels.append(rng.randint(0, 99))
        large_queries.append(SimpleQuery(BoundingBox((xmin, ymin, tmin),
                                                     (xmax, ymax, tmax)),
                                         labels))

    sequenced_queries = []
    for i in range(20):
        seq = []

        xmin = rng.uniform(0, 800)
        ymin = rng.uniform(0, 800)
        tmin = rng.randint(5000, 10000)
        xmax = xmin + 1000
        ymax = ymin + 1000
        tmax = tmin + 40000
        for j in range(rng.randint(3, 6)):
            dist = j * 100
            duration = j * 200
            labels = []
            for i in range(rng.randint(30, 60)):
                labels.append(rng.randint(0, 99))
            seq.append(SimpleQuery(BoundingBox((xmin + dist, ymin + dist, tmin + duration),
                                               (xmax + dist, ymax + dist, tmax + duration)),
                                   labels))
        sequenced_queries.append(seq)

    large_results = [Query("RANDOM-WALK-LARGE-{}".format(i), sq)
                     for i, sq in enumerate(large_queries)]
    small_results = [Query("RANDOM-WALK-SMALL-{}".format(i), sq)
                     for i, sq in enumerate(small_queries)]
    sequenced = [Query("RANDOM-WALK-SEQUENCED-{}".format(i), *sq)
                 for i, sq in enumerate(sequenced_queries)]
    return {"large": large_results, "small": small_results, "sequenced": sequenced}


# Runs a single query for a given tree and returns the stats.
def run_query(tree, query, results=None, logfile=None):
    stats_path = TMP_PATH / "query-stats.json"
    common.remove(stats_path)

    args = [
        str(QUERY),
        "--tree", str(tree),
        "--stats", str(stats_path),
    ]
    if results is not None:
        args.extend(["--results", str(results)])
    for sq in query.queries:
        mbb = "{}, {}, {}, {}, {}, {}" \
            .format(sq.mbb.min[0], sq.mbb.max[0],
                    sq.mbb.min[1], sq.mbb.max[1],
                    sq.mbb.min[2], sq.mbb.max[2])
        labels = ",".join(map(str, sq.labels))
        args.extend(["-r", mbb, "-l", labels])

    # Clear OS cache before querying the tree.
    # See the comment at the top of drop_cache for permissions.
    subprocess.check_call([str(SCRIPTS_PATH / "drop_cache")])

    print("Running query {} on tree {}:".format(
        query.name, tree), file=logfile, flush=True)
    print("============================\n", file=logfile, flush=True)
    subprocess.check_call(args, stdout=logfile)
    print("\n\n", file=logfile, flush=True)

    with stats_path.open("r") as stats_file:
        return json.load(stats_file)


def tree_stats(tree):
    output = subprocess.check_output([
        str(STATS),
        "--tree", str(tree),
    ])
    return json.loads(output.decode("utf-8"))


def measure_queries(tree, query_set, logfile=None):
    # Evaluate every query for the given tree and return the individual
    # results as well as the average.
    def measure_set(queries):
        query_stats = {query: run_query(tree, query, logfile=logfile) for query in queries}

        def get_stats(key):
            query_values = {query: stats[key] for query, stats in query_stats.items()}
            average = sum(query_values.values()) / max(len(query_values), 1)
            return {"values": map_keys(query_values, lambda q: q.name), "avg": average}

        return {"duration": get_stats("duration"), "total_io": get_stats("total_io")}

    # For every query set: Evaluate every query.
    return {
        set_name: measure_set(queries) for set_name, queries in query_set.items()
    }


if __name__ == "__main__":
    compile(debug_stats=True)

    geolife_trees = [
        OUTPUT_PATH / "geolife-obo",
        OUTPUT_PATH / "geolife-hilbert",
        OUTPUT_PATH / "geolife-str-lf",
        OUTPUT_PATH / "geolife-quickload",
        OUTPUT_PATH / "variants" / "geolife-str-plain",
        OUTPUT_PATH / "variants" / "geolife-str-ll",
        OUTPUT_PATH / "variants" / "geolife-obo-beta-increasing",
        OUTPUT_PATH / "variants" / "geolife-obo-beta-decreasing",
        OUTPUT_PATH / "variants" / "geolife-quickload-beta-increasing",
        OUTPUT_PATH / "variants" / "geolife-quickload-beta-decreasing",
        OUTPUT_PATH / "variants" / "geolife-obo-beta-0.25",
        OUTPUT_PATH / "variants" / "geolife-obo-beta-0.75",
        OUTPUT_PATH / "variants" / "geolife-obo-beta-1.0",
        OUTPUT_PATH / "variants" / "geolife-quickload-beta-0.25",
        OUTPUT_PATH / "variants" / "geolife-quickload-beta-0.75",
        OUTPUT_PATH / "variants" / "geolife-quickload-beta-1.0",
        OUTPUT_PATH / "variants" / "geolife-shuffled-quickload",
    ]
    geolife_queries = get_geolife_queries()

    osm_trees = [
        OUTPUT_PATH / "osm-obo",
        OUTPUT_PATH / "osm-hilbert",
        OUTPUT_PATH / "osm-str-lf",
        OUTPUT_PATH / "osm-quickload",
        OUTPUT_PATH / "variants" / "osm-str-plain",
        OUTPUT_PATH / "variants" / "osm-str-ll",
        OUTPUT_PATH / "variants" / "osm-obo-beta-increasing",
        OUTPUT_PATH / "variants" / "osm-obo-beta-decreasing",
        OUTPUT_PATH / "variants" / "osm-quickload-beta-increasing",
        OUTPUT_PATH / "variants" / "osm-quickload-beta-decreasing",
        OUTPUT_PATH / "variants" / "osm-obo-beta-0.25",
        OUTPUT_PATH / "variants" / "osm-obo-beta-0.75",
        OUTPUT_PATH / "variants" / "osm-obo-beta-1.0",
        OUTPUT_PATH / "variants" / "osm-quickload-beta-0.25",
        OUTPUT_PATH / "variants" / "osm-quickload-beta-0.75",
        OUTPUT_PATH / "variants" / "osm-quickload-beta-1.0",
        OUTPUT_PATH / "variants" / "osm-shuffled-quickload",
    ]
    osm_queries = get_osm_queries()

    random_walk_trees = [
        OUTPUT_PATH / "random-walk-obo",
        OUTPUT_PATH / "random-walk-hilbert",
        OUTPUT_PATH / "random-walk-str-lf",
        OUTPUT_PATH / "random-walk-quickload",
    ]
    random_walk_queries = get_random_walk_queries()

    def handle_tree(tree, query_set, results, stats):
        tree_name = tree.name
        print("Running queries on tree {}.".format(tree_name))
        results[tree_name] = measure_queries(tree, query_set, logfile)
        stats[tree_name] = tree_stats(tree)

    with (OUTPUT_PATH / "eval_query.log").open("w") as logfile:
        datasets = [
            ("geolife", geolife_trees, geolife_queries),
            ("osm", osm_trees, osm_queries),
            ("random-walk", random_walk_trees, random_walk_queries)
        ]

        result = {
            "geolife": {},
            "osm": {},
            "random-walk": {},
        }
        stats = {
            "geolife": {},
            "osm": {},
            "random-walk": {},
        }
        for dataset_name, tree_set, query_set in datasets:
            for tree in tree_set:
                handle_tree(tree, query_set, result[
                            dataset_name], stats[dataset_name])

        # bloom filters need special treatment.
        compile(bloom_filters=True, debug_stats=True)
        for tree in [OUTPUT_PATH / "variants" / "geolife-obo-bloom",
                     OUTPUT_PATH / "variants" / "geolife-quickload-bloom"]:
            handle_tree(tree, geolife_queries,
                        result["geolife"], stats["geolife"])

        for tree in [OUTPUT_PATH / "variants" / "osm-obo-bloom",
                     OUTPUT_PATH / "variants" / "osm-quickload-bloom"]:
            handle_tree(tree, osm_queries, result["osm"], stats["osm"])

        # as do trees with different fanout.
        for fanout in [32, 50, 64]:
            compile(leaf_fanout=fanout, internal_fanout=fanout, debug_stats=True)
            handle_tree(OUTPUT_PATH / "variants" / "geolife-quickload-fanout-{}".format(fanout),
                        geolife_queries, result["geolife"], stats["geolife"])

        # Restore default config to avoid errors.
        compile()

    with (RESULT_PATH / "queries.json").open("w") as outfile:
        json.dump(result, outfile, sort_keys=True, indent=4)

    with (RESULT_PATH / "tree_stats.json").open("w") as outfile:
        json.dump(stats, outfile, sort_keys=True, indent=4)
