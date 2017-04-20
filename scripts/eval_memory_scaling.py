#!/usr/bin/env python3

import commands
from common import DATA_PATH, TMP_PATH, RESULT_PATH
from compile import compile
from datasets import GEOLIFE

if __name__ == "__main__":
    compile()

    dataset = GEOLIFE

    # In Megabyte.
    # Higher values arent possible right now because quickload requires 1 file
    # stream for each node. Number of open files is limited by the OS though.
    sizes = [32, 64, 128]

    algorithms = ["hilbert", "str-lf", "quickload"]

    tree_path = TMP_PATH / "memory_scaling_tree"

    table = PrettyTable([
        "Algorithm", "Memory (MB)", "I/O",
        "Duration"]
    )
    table.align["I/O"] = "r"
    table.align["Duration"] = "r"

    with (OUTPUT_PATH / "eval_tree_building.log").open("w") as logfile:
        for algorithm in algorithms:
            for size in sizes:
                _, data_path = dataset
                result = commands.build_tree(algorithm, tree_path, data_path, logfile,
                                             memory=size)
                table.add_row([algorithm, memory,
                               result.total_io, result.duration])

    with (RESULT_PATH / "eval_memory_scaling.txt") as outfile:
        print(table, file=outfile)
