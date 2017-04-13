#!/usr/bin/env python3

import collections
import itertools
import json
import subprocess
import resource

import common
from common import LOADER, RESULT_PATH, OUTPUT_PATH, TMP_PATH, DATA_PATH
from compile import compile
from datasets import OSM_VARYING_SIZE, RANDOM_WALK_VARYING_SIZE
from lib.prettytable import PrettyTable

Result = collections.namedtuple("Result", ["total_io", "duration"])


def annotate(value, tuples):
    for t in tuples:
        yield (value,) + t


def run(algorithm, tree_path, entries_path, stats_path, logfile):
    # Make sure the tree does not exist yet.
    common.remove(tree_path)

    tpie_temp_path = common.make_dir(TMP_PATH / "tpie")
    subprocess.check_call([
        str(LOADER),
        "--algorithm", algorithm,
        "--entries", str(entries_path),
        "--tree", str(tree_path),
        "--tmp", str(tpie_temp_path),
        "--stats", str(stats_path)
    ], stdout=logfile)

    stats = None
    with stats_path.open() as stats_file:
        stats = json.load(stats_file)

    return Result(total_io=stats["total_io"],
                  duration=stats["duration"])


if __name__ == "__main__":
    compile()

    # Make sure that we can open enough files (quickload requires many
    # buckets). This is equivalent to `ulimit -Sn 16384` in the shell.
    limits = resource.getrlimit(resource.RLIMIT_NOFILE)
    resource.setrlimit(resource.RLIMIT_NOFILE, (2 ** 14, limits[1]))

    algorithms = [
        # "obo," # TODO: so slow..
        "hilbert",
        "str",
        "quickload"
    ]

    stats_dir = common.reset_dir(OUTPUT_PATH / "build_tree_stats")
    tree_dir = common.reset_dir(OUTPUT_PATH / "trees")

    table = PrettyTable(["Algorithm", "Dataset", "Entries", "I/O", "Duration"])
    table.align["Entries"] = "r"
    table.align["I/O"] = "r"
    table.align["Duration"] = "r"

    with (OUTPUT_PATH / "build_trees.log").open("w") as logfile:
        dataset = list(itertools.chain(annotate("osm", OSM_VARYING_SIZE),
                                       annotate("random-walk", RANDOM_WALK_VARYING_SIZE)))
        for algorithm in algorithms:
            for data_kind, entries, _, data_path in dataset:
                basename = "{}-{}-n{}".format(data_kind, algorithm, entries)
                stats_path = stats_dir / (basename + ".json")
                tree_path = tree_dir / basename

                print("Running {} on {} entries from {}"
                      .format(algorithm, entries, data_kind))
                result = run(algorithm, tree_path,
                             data_path, stats_path, logfile)
                table.add_row([
                    algorithm, data_kind, entries, result.total_io, result.duration
                ])

                print("\n\n", file=logfile, flush=True)

    with (RESULT_PATH / "build_trees.txt").open("w") as outfile:
        print(table, file=outfile)
