#!/usr/bin/env python3

import subprocess

from common import STATS
from common import RESULT_PATH, OUTPUT_PATH


def tree_stats(outfile, trees):
    for t in trees:
        out = subprocess.check_output([
            str(STATS),
            "--tree", str(t)
        ]).decode("utf-8")
        print(out, file=outfile)

if __name__ == "__main__":
    def name(p, dataset):
        return p.format(ds=dataset)

    def trees(dataset):
        return [
            OUTPUT_PATH / "variants" / name("{ds}-quickload-beta-0.25", dataset),
            OUTPUT_PATH / name("{ds}-quickload", dataset),
            OUTPUT_PATH / "variants" / name("{ds}-quickload-beta-0.75", dataset),
            OUTPUT_PATH / "variants" / name("{ds}-quickload-beta-1.0", dataset),
            OUTPUT_PATH / "variants" / name("{ds}-quickload-beta-increasing", dataset),
            OUTPUT_PATH / "variants" / name("{ds}-quickload-beta-decreasing", dataset),
        ]

    args = [
        ("geolife-beta.stats", trees("geolife")),
        ("osm-beta.stats", trees("osm")),
        ("random-walk-beta.stats", trees("random-walk")),
    ]

    for filename, trees in args:
        with (RESULT_PATH / filename).open("w") as outfile:
            print("Creating {}".format(filename))
            tree_stats(outfile, trees)
