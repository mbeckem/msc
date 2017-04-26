#!/usr/bin/env python3

import json
import subprocess

import common
from common import OUTPUT_PATH, RESULT_PATH, TMP_PATH, QUERY
from compile import compile


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

    def __init__(self, *queries):
        self.queries = list(queries)

    def count():
        return len(self.queries)


def geolife_queries():
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
        SimpleQuery(all, [1, 4]),
    ]

    small_queries = [
        # ~ 14k units (labels are very rare, this is the logical OR of the labels).
        SimpleQuery(all, [7, 8, 10, 11]),

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
                                (39.9743, 116.4199, 1228867298)), [label("bike")])
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
    large_results = [Query(sq) for sq in large_queries]

    # Queries that select only few units.
    small_results = [Query(sq) for sq in small_queries]

    # Sequenced query, i.e. multiple simple queries that have to be satisfied.
    sequenced = [Query(*sq) for sq in sequenced_queries]

    return large_results, small_results, sequenced


def osm_queries():
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
                                (52.5, 13.8, 35000)), [])
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
        ]
        # TODO MORE
    ]

    large_results = [Query(sq) for sq in large_queries]
    small_results = [Query(sq) for sq in small_queries]
    sequenced = [Query(*sq) for sq in sequenced_queries]

    return large_results, small_results, sequenced


def run_query(tree, query, results=None):
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
        mbb = "({}, {}, {}, {}, {}, {})" \
            .format(sq.mbb.min[0], sq.mbb.max[0],
                    sq.mbb.min[1], sq.mbb.max[1],
                    sq.mbb.min[2], sq.mbb.max[2])
        labels = ",".join(map(str, sq.labels))
        args.extend(["-r", mbb, "-l", labels])

    print(args)

    subprocess.check_call(args)
    with stats_path.open("r") as stats_file:
        return json.load(stats_file)


if __name__ == "__main__":
    compile(debug_stats=True)

    geolife_queries()
    osm_queries()

    # for q in GEOLIFE_SINGLE_LARGE_RESULTS:
    #    print(run_query(OUTPUT_PATH / "geolife-obo", q))

    # for q in GEOLIFE_SINGLE_SMALL_RESULTS:
    #    print(run_query(OUTPUT_PATH / "geolife-obo", q))

    # for q in GEOLIFE_SEQUENCED:
    #    print(run_query(OUTPUT_PATH / "geolife-obo", q))

    print(run_query("/home/michael/Projects/msc/osm-hilbert",
                    osm_queries()[0][2], results=TMP_PATH / "results.txt"))
