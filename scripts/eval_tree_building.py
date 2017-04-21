#!/usr/bin/env python3

import collections
import itertools

import common
import commands

from common import LOADER, RESULT_PATH, OUTPUT_PATH, TMP_PATH, DATA_PATH
from compile import compile
from datasets import GEOLIFE, OSM_ROUTES, RANDOM_WALK
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

    table = PrettyTable(["Algorithm", "Dataset", "Entries",
                         "Total I/O", "Reads", "Writes", "Duration"])
    table.align["Entries"] = "r"
    table.align["Total I/O"] = "r"
    table.align["Reads"] = "r"
    table.align["Writes"] = "r"
    table.align["Duration"] = "r"

    with (OUTPUT_PATH / "eval_tree_building.log").open("w") as logfile:
        dataset = [("geolife", *GEOLIFE),
                   ("osm", *OSM_ROUTES),
                   ("random-walk", *RANDOM_WALK)]
        dataset_steps = list(itertools.chain.from_iterable(
            itertools.starmap(create_dataset_steps, dataset)))

        # Create trees using all algorithms, with increasing step size.
        for algorithm in algorithms:
            for data_kind, entries, data_path in dataset_steps:
                # Overwrite earlier trees with less entries.
                basename = "{}-{}".format(data_kind, algorithm)
                tree_path = OUTPUT_PATH / basename

                print("Running {} on {} entries from {}"
                      .format(algorithm, entries, data_kind))
                result = commands.build_tree(algorithm, tree_path,
                                             data_path, logfile, limit=entries)
                table.add_row([
                    algorithm, data_kind, entries,
                    result.total_io, result.read_io, result.write_io,
                    result.duration
                ])

        # Incremetally build a tree using one-by-one insertion.
        # Insert a given batch of items, take stats, insert the next batch.
        # This does not influence the results in a meaningful way because
        # one by one insertion works on a the unit of a single entry.
        for data_kind, entries, data_path in dataset:
            basename = "{}-obo".format(data_kind, entries)
            tree_path = OUTPUT_PATH / basename

            print("Running one by one insertion on entries from {}".format(data_kind))
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
                result = commands.build_tree("obo", tree_path,
                                             data_path, logfile,
                                             offset=offset, limit=step_size,
                                             keep_existing=True)
                total_io += result.total_io
                read_io += result.read_io
                write_io += result.write_io
                duration += result.duration

                table.add_row([
                    "obo", data_kind, size,
                    total_io, read_io, write_io,
                    duration
                ])

    with (RESULT_PATH / "eval_tree_building.txt").open("w") as outfile:
        print(table, file=outfile)
