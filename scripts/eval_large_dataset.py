#!/usr/bin/env python3
# Creates trees for a large dataset with varying
# amounts of RAM.

import commands
import common
from common import TMP_PATH, OUTPUT_PATH, RESULT_PATH
from compile import compile
from datasets import RANDOM_WALK_LARGE
from lib.prettytable import PrettyTable

if __name__ == "__main__":
    compile()

    algorithms = ["hilbert", "quickload"]
    sizes = [32, 128, 256]  # Megabyte
    tree_path = TMP_PATH / "large_dataset_tree"

    table = PrettyTable([
        "Algorithm", "Memory (MB)", "I/O",
        "Duration"
    ])
    table.align["I/O"] = "r"
    table.align["Duration"] = "r"

    dataset = RANDOM_WALK_LARGE

    print("Using dataset {}".format(dataset))
    with (OUTPUT_PATH / "eval_large_dataset.log").open("w") as logfile:
        for algorithm in algorithms:
            for size in sizes:
                print("Running algorithm {} with memory size {} MB".format(
                    algorithm, size))

                _, data_path = dataset
                result = commands.build_tree(algorithm, tree_path, data_path, logfile,
                                             memory=size)
                table.add_row([algorithm, size,
                               result.total_io, result.duration])

    with (RESULT_PATH / "eval_large_dataset.txt").open("w") as outfile:
        print("Using dataset {} with different algorithms and RAM.")
        print(table, file=outfile)
