#!/usr/bin/env python3
# Build trees with many algorithms varying number of entries.
# For every algorithm, we only keep the largest tree on disk,
# but we keep the stats for all of them.

import collections
import itertools
import json

import common

from common import LOADER, RESULT_PATH, OUTPUT_PATH, TMP_PATH, DATA_PATH
from common import GEOLIFE, OSM_ROUTES, RANDOM_WALK
from common import compile
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
    compile()

    algorithms = [
        "hilbert",
        "str-lf",
        "quickload"
    ]

    results = []
    with (OUTPUT_PATH / "tree_building_beta_0.9.log").open("w") as logfile:
        dataset = [("geolife", *GEOLIFE),
                   ("osm", *OSM_ROUTES)]
        dataset_steps = list(itertools.chain.from_iterable(
            itertools.starmap(create_dataset_steps, dataset)))

        # Create trees using all algorithms, with increasing step size.
        for algorithm in algorithms:
            for dataset_name, entries, data_path in dataset_steps:
                # Overwrite earlier trees with fewer entries.
                basename = "{}-{}-beta-0.9".format(dataset_name, algorithm)
                tree_path = OUTPUT_PATH / basename

                print("Running {} on {} entries from {}"
                      .format(algorithm, entries, dataset_name))
                result = common.build_tree(algorithm, tree_path,
                                           data_path, logfile, beta=0.9,
                                           limit=entries)
                results.append({
                    "dataset": dataset_name,
                    "algorithm": algorithm,
                    "entries": entries,
                    **result
                })

        # Incremetally build a tree using one-by-one insertion.
        # Insert a given batch of items, take stats, insert the next batch.
        # This does not influence the results in a meaningful way because
        # one by one insertion works on the unit of a single entry.
        for dataset_name, entries, data_path in dataset:
            basename = "{}-obo-beta-0.9".format(dataset_name, entries)
            tree_path = OUTPUT_PATH / basename

            print("Running one by one insertion on entries from {}".format(dataset_name))
            last_size = 0
            total_io = 0
            read_io = 0
            write_io = 0
            duration = 0
            for size in steps(entries):
                offset = last_size
                step_size = size - last_size
                last_size = size

                print("-- Entries from {} to {}".format(
                    offset, offset + step_size))
                result = common.build_tree("obo", tree_path,
                                           data_path, logfile,
                                           beta=0.9,
                                           offset=offset, limit=step_size,
                                           keep_existing=(last_size > 0))
                total_io += result["total_io"]
                read_io += result["read_io"]
                write_io += result["write_io"]
                duration += result["duration"]

                results.append({
                    "dataset": dataset_name,
                    "algorithm": "obo",
                    "entries": size,
                    "total_io": total_io,
                    "read_io": read_io,
                    "write_io": write_io,
                    "duration": duration,
                })

    with (RESULT_PATH / "tree_building_beta_0.9.txt").open("w") as outfile:
        table = PrettyTable(["Algorithm", "Dataset", "Entries",
                             "Total I/O", "Reads", "Writes", "Duration"])
        table.align["Entries"] = "r"
        table.align["Total I/O"] = "r"
        table.align["Reads"] = "r"
        table.align["Writes"] = "r"
        table.align["Duration"] = "r"

        for result in results:
            table.add_row([
                result["algorithm"],
                result["dataset"],
                result["entries"],
                result["total_io"],
                result["read_io"],
                result["write_io"],
                result["duration"],
            ])

        print(table, file=outfile)

    with (RESULT_PATH / "tree_building_beta_0.9.json").open("w") as outfile:
        json.dump(results, outfile, sort_keys=True, indent=4)
