#!/usr/bin/env python3

import collections
import itertools
import json
import subprocess

import common
import commands
import datasets
from common import LOADER, RESULT_PATH, OUTPUT_PATH, TMP_PATH, DATA_PATH
from compile import compile
from lib.prettytable import PrettyTable


if __name__ == "__main__":
    table = PrettyTable(["Algorithm", "Entries", "Leaf fanout", "Internal fanout",
                         "I/O", "Duration"])
    table.align["Leaf fanout"] = "r"
    table.align["Internal fanout"] = "r"
    table.align["Entries"] = "r"
    table.align["I/O"] = "r"
    table.align["Duration"] = "r"

    fanouts = [32, 50, 64, 0]
    algorithms = [
        "hilbert",
        "str",
        "quickload",
        #  "obo", # TODO SOO SLOW
    ]

    # TODO Measure query performance as well.
    with (OUTPUT_PATH / "eval_fanout.log").open("w") as logfile:
        entries = 1000000
        data_path = datasets.osm_generated(entries)

        results = collections.defaultdict(dict)

        for fanout in fanouts:
            compile(leaf_fanout=fanout, internal_fanout=fanout)

            for algorithm in algorithms:
                print("Running algorithm {} for fanout {}"
                      .format(algorithm, fanout))

                tree_path = TMP_PATH / "eval_fanout_tree"

                result = commands.build_tree(algorithm, tree_path,
                                             data_path, logfile)
                results[algorithm][fanout] = result

        for algorithm, fanout in itertools.product(algorithms, fanouts):
            result = results[algorithm][fanout]
            table.add_row([
                algorithm, entries, result.leaf_fanout, result.internal_fanout,
                result.total_io, result.duration
            ])

    with (RESULT_PATH / "eval_fanout.txt").open("w") as outfile:
        print(table, file=outfile)
