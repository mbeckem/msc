#!/usr/bin/env python3
# Creates a tree with only few entries and very low fanout.
# Also builds the inspector tool.

import itertools

import common
from common import OUTPUT_PATH, RESULT_PATH
from common import compile
from datasets import RANDOM_WALK_SMALL

if __name__ == "__main__":
    compile(internal_fanout=16, leaf_fanout=16)

    entries_path = RANDOM_WALK_SMALL[1]

    with (OUTPUT_PATH / "irwi_small_tree.log").open("w") as logfile:
        for algorithm, beta in itertools.product(["obo", "quickload"], [0.5, 1.0]):
            common.build_tree(algorithm, OUTPUT_PATH / "irwi_small_{}_b{}".format(algorithm, beta),
                              entries_path, logfile, beta=beta, limit=4096)

        for algorithm in ["hilbert", "str-plain"]:
            common.build_tree(algorithm, OUTPUT_PATH / "irwi_small_{}".format(algorithm),
                              entries_path, logfile, limit=4096)
