#!/usr/bin/env python3

import commands
from common import OUTPUT_PATH, RESULT_PATH
from compile import compile
from datasets import GEOLIFE

if __name__ == "__main__":
    compile(internal_fanout=16, leaf_fanout=16, build_inspector=True)

    with (OUTPUT_PATH / "irwi_small_tree.log").open("w") as logfile:
        commands.build_tree("obo", RESULT_PATH / "irwi_small",
                            GEOLIFE[1], logfile, limit=1024)
