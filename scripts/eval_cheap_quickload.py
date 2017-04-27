#!/usr/bin/env python3
# Compares the "cheap" quickload variant against the normal one
# by building trees and comparing the required time and IOs.

import json

import commands
import common
from common import DATA_PATH, TMP_PATH, RESULT_PATH, OUTPUT_PATH
from compile import compile
from datasets import GEOLIFE, OSM_ROUTES, RANDOM_WALK
from lib.prettytable import PrettyTable

if __name__ == "__main__":
    tree_dir = common.reset_dir(OUTPUT_PATH / "cheap_quickload")

    def tree_path(dataset, cheap):
        return tree_dir / "{}-{}".format(dataset, "cheap" if cheap else "normal")

    with (OUTPUT_PATH / "cheap_quickload.log").open("w") as logfile:
        def run(cheap):
            compile(cheap_quickload=cheap)

            datasets = [("geolife", *GEOLIFE),
                        ("osm", *OSM_ROUTES),
                        ("random-walk", *RANDOM_WALK)]
            results = []
            for dataset_name, entries, data_path in datasets:
                print("quickload (cheap: {}) on {} entries from {}".format(
                    cheap, entries, dataset_name))

                result = commands.build_tree("quickload",
                                             tree_path(dataset_name, cheap),
                                             data_path, logfile)
                results.append({
                    "algorithm": "quickload" if not cheap else "quickload-cheap",
                    "entries": entries,
                    "dataset": dataset_name,
                    **result._asdict()
                })
            return results

        results = run(True) + run(False)

    with (RESULT_PATH / "cheap_quickload.json").open("w") as outfile:
        json.dump(results, outfile, indent=4, sort_keys=True)

    with (RESULT_PATH / "cheap_quickload.txt").open("w") as outfile:
        table = PrettyTable([
            "Dataset", "Algorithm", "Entries", "I/O", "Duration"
        ])
        table.align["Entries"] = "r"
        table.align["I/O"] = "r"
        table.align["Duration"] = "r"

        for result in results:
            table.add_row([
                result["dataset"],
                result["algorithm"],
                result["entries"],
                result["total_io"],
                result["duration"],
            ])

        print(table, file=outfile)
