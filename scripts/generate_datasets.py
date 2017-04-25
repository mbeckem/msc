#!/usr/bin/env python3

import subprocess

from common import GENERATOR, OUTPUT_PATH
from datasets import RANDOM_WALK_VARYING_LABELS

if __name__ == "__main__":
    with (OUTPUT_PATH / "generate_datasets.log").open("w") as logfile:
        for entries, labels, path in RANDOM_WALK_VARYING_LABELS:
            if path.exists():
                continue

            print("Generating {}".format(path))
            subprocess.check_call([
                str(GENERATOR),
                "--output", str(path),
                "-n", str(entries),
                "-l", str(labels),
            ], stdout=logfile)
