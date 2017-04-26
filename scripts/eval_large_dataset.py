#!/usr/bin/env python3
# Creates trees for a large dataset with varying
# amounts of RAM.

import json

import commands
import common
from common import TMP_PATH, OUTPUT_PATH, RESULT_PATH
from compile import compile
from datasets import RANDOM_WALK_LARGE
from lib.prettytable import PrettyTable

if __name__ == "__main__":
    compile()

    algorithms = ["hilbert", "str-lf", "str-plain", "quickload"]
    sizes = [64, 128, 256]  # Megabyte
    tree_path = TMP_PATH / "large_dataset_tree"

    dataset = RANDOM_WALK_LARGE

    print("Using dataset {}".format(dataset))
    results = []
    with (OUTPUT_PATH / "large_dataset.log").open("w") as logfile:
        for algorithm in algorithms:
            for size in sizes:
                print("Running algorithm {} with memory size {} MB".format(
                    algorithm, size))

                _, data_path = dataset
                result = commands.build_tree(algorithm, tree_path, data_path, logfile,
                                             memory=size)
                results.append({
                    "algorithm": algorithm,
                    "memory": size,
                    "total_io": result.total_io,
                    "duration": result.duration,
                })

    with (RESULT_PATH / "large_dataset.txt").open("w") as outfile:
        table = PrettyTable([
            "Algorithm", "Memory (MB)", "I/O",
            "Duration"
        ])
        table.align["I/O"] = "r"
        table.align["Duration"] = "r"
        for result in results:
            table.add_row([
                result["algorithm"],
                result["memory"],
                result["total_io"],
                result["duration"],
            ])

        print("Using dataset {} with different algorithms and RAM.".format(
            dataset), file=outfile)
        print(table, file=outfile)

    with (RESULT_PATH / "large_dataset.json").open("w") as outfile:
        json.dump(results, outfile, sort_keys=True, indent=4)
