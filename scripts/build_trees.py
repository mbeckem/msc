#!/usr/bin/env python3

import collections
import itertools
import json
import subprocess

import common
import commands

from common import LOADER, RESULT_PATH, OUTPUT_PATH, TMP_PATH, DATA_PATH
from compile import compile
from datasets import GEOLIFE, OSM_ROUTES, RANDOM_WALK
from lib.prettytable import PrettyTable


if __name__ == "__main__":
    compile()

    algorithms = [
        # "obo," # TODO: so slow..
        "hilbert",
        "str-lf",
        "quickload"
    ]

    tree_dir = common.reset_dir(OUTPUT_PATH / "trees")

    table = PrettyTable(["Algorithm", "Dataset", "Entries", "I/O", "Duration"])
    table.align["Entries"] = "r"
    table.align["I/O"] = "r"
    table.align["Duration"] = "r"

    with (OUTPUT_PATH / "build_trees.log").open("w") as logfile:
        def size_steps(name, max_entries, path):
            step = int(max_entries / 10)
            for i in range(1, 11):
                yield (name, i * step, path)

        dataset = list(itertools.chain(size_steps("geolife", *GEOLIFE),
                                       size_steps("osm", *OSM_ROUTES),
                                       size_steps("random-walk", *RANDOM_WALK)))
        for algorithm in algorithms:
            for data_kind, entries, data_path in dataset:
                basename = "{}-{}-n{}".format(data_kind, algorithm, entries)
                tree_path = tree_dir / basename

                print("Running {} on {} entries from {}"
                      .format(algorithm, entries, data_kind))
                result = commands.build_tree(algorithm, tree_path,
                                             data_path, logfile, limit=entries)
                table.add_row([
                    algorithm, data_kind, entries, result.total_io, result.duration
                ])

    with (RESULT_PATH / "build_trees.txt").open("w") as outfile:
        print(table, file=outfile)
