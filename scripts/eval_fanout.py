#!/usr/bin/env python3
# Creates trees with different fanouts and gathers statistics.

import collections
import itertools
import json

import common
from common import LOADER, RESULT_PATH, OUTPUT_PATH, TMP_PATH, DATA_PATH
from common import compile
from common import GEOLIFE
from lib.prettytable import PrettyTable


if __name__ == "__main__":
    fanouts = [32, 50, 64, 0]
    algorithms = [
        "hilbert",
        "str-lf",
        "quickload",
        "obo"
    ]

    # TODO Measure query performance as well.
    with (OUTPUT_PATH / "fanouts.log").open("w") as logfile:
        dataset = ("geolife", *GEOLIFE)

        # fanout -> algorithm -> list of results
        results = collections.defaultdict(
            lambda: collections.defaultdict(list))
        for fanout in fanouts:
            compile(leaf_fanout=fanout, internal_fanout=fanout)

            for algorithm in algorithms:
                dataset_name, entries, data_path = dataset
                tree_path = TMP_PATH / "eval_fanout_tree"

                # Reduced number of entries
                entries = entries // 10

                print("Running {} on {} entries from {} (fanout = {})"
                      .format(algorithm, entries, dataset_name, fanout))
                result = common.build_tree(algorithm, tree_path,
                                           data_path, logfile, limit=entries)
                results[fanout][algorithm].append({
                    "dataset": dataset_name,
                    "algorithm": algorithm,
                    "entries": entries,
                    "fanout": fanout,
                    **result
                })

    with (RESULT_PATH / "fanouts.json").open("w") as outfile:
        json.dump(results, outfile, indent=4, sort_keys=True)
