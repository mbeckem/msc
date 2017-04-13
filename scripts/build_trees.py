#!/usr/bin/env python3

import collections
import itertools
import json
import subprocess

import common
import commands

from common import LOADER, RESULT_PATH, OUTPUT_PATH, TMP_PATH, DATA_PATH
from compile import compile
from datasets import OSM_VARYING_SIZE, RANDOM_WALK_VARYING_SIZE
from lib.prettytable import PrettyTable


def annotate(value, tuples):
    for t in tuples:
        yield (value,) + t

if __name__ == "__main__":
    compile()

    algorithms = [
        # "obo," # TODO: so slow..
        "hilbert",
        "str",
        "quickload"
    ]

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
                tree_path = tree_dir / basename

                print("Running {} on {} entries from {}"
                      .format(algorithm, entries, data_kind))
                result = commands.build_tree(algorithm, tree_path,
                                             data_path, logfile)
                table.add_row([
                    algorithm, data_kind, entries, result.total_io, result.duration
                ])

    with (RESULT_PATH / "build_trees.txt").open("w") as outfile:
        print(table, file=outfile)
