#!/usr/bin/env python3
# Compares the "cheap" quickload variant against the normal one
# by building trees and comparing the required time and IOs.

import itertools

import commands
import common
from common import DATA_PATH, TMP_PATH, RESULT_PATH, OUTPUT_PATH
from compile import compile
from datasets import GEOLIFE, OSM_ROUTES
from lib.prettytable import PrettyTable


# Iterate from 0 to max in 10 steps.
def steps(max):
    step = int(max / 10)
    for i in range(1, 11):
        yield step * i


# Map a dataset entry to 10 entries with increasing step size.
def create_dataset_steps(name, max_entries, path):
    return ((name, step, path) for step in steps(max_entries))


if __name__ == "__main__":
    tree_dir = common.reset_dir(OUTPUT_PATH / "cheap_quickload")

    def tree_path(dataset, cheap):
        return tree_dir / "{}-{}".format(dataset, "cheap" if cheap else "normal")

    table = PrettyTable([
        "Dataset", "Algorithm", "Entries", "I/O", "Duration"
    ])
    table.align["Entries"] = "r"
    table.align["I/O"] = "r"
    table.align["Duration"] = "r"

    with (OUTPUT_PATH / "eval_cheap_quickload.log").open("w") as logfile:
        def run(cheap):
            compile(cheap_quickload=cheap)

            dataset = [("geolife", *GEOLIFE),
                       ("osm", *OSM_ROUTES)]
            dataset_steps = list(itertools.chain.from_iterable(
                itertools.starmap(create_dataset_steps, dataset)))

            results = []
            for dataset_name, entries, data_path in dataset_steps:
                print("quickload (cheap: {}) on {} entries from {}".format(
                    cheap, entries, dataset_name))

                result = commands.build_tree("quickload",
                                             tree_path(dataset_name, cheap),
                                             data_path, logfile,
                                             limit=entries, memory=32)
                results.append({
                    "entries": entries,
                    "dataset": dataset_name,
                    **result._asdict()
                })
            return results

        results_normal = run(False)
        results_cheap = run(True)

        for result in results_normal:
            table.add_row([
                result["dataset"],
                "quickload",
                result["entries"],
                result["total_io"],
                result["duration"],
            ])

        for result in results_cheap:
            table.add_row([
                result["dataset"],
                "quickload-cheap",
                result["entries"],
                result["total_io"],
                result["duration"],
            ])

    with (RESULT_PATH / "eval_cheap_quickload.txt").open("w") as outfile:
        print(table, file=outfile)
