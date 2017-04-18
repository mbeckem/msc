#!/usr/bin/env python3

import collections
import json
import shutil
import subprocess
from pathlib import Path

import common
import commands
import format

from common import DATA_PATH, TMP_PATH, OUTPUT_PATH, RESULT_PATH, LOADER
from compile import compile
from datasets import RANDOM_WALK_VARYING_LABELS, OSM_ROUTES
from lib.prettytable import PrettyTable

Result = collections.namedtuple(
    "Result", ["labels", "algorithm", "total_io", "duration", "index_size", "tree_size", "total_size"])


def build_tree(algorithm, entries_path):
    tree_path = TMP_PATH / "node_building_tmp_tree"
    stats = commands.build_tree(algorithm, tree_path,
                                entries_path, logfile)

    total_size = common.file_size(tree_path)
    tree_size = common.file_size(tree_path / "tree.blocks")
    index_size = total_size - tree_size

    return {
        "algorithm": algorithm,
        "tree_size": tree_size,
        "index_size": index_size,
        "total_size": total_size,
        **stats._asdict()
    }


def run_random_walk(naive_node_building, logfile):
    compile(naive_node_building=naive_node_building)

    results = []
    for entries, labels, entries_path in RANDOM_WALK_VARYING_LABELS:
        print("entries = {}, labels = {}, path = {}"
              .format(entries, labels, entries_path))
        result = build_tree("hilbert", entries_path)
        result["algorithm"] = "hilbert"
        results.append(result)
    return results


def run_others(naive_node_building, logfile):
    compile(naive_node_building=naive_node_building)

    results = []
    algorithms = ["hilbert", "str-plain", "str-lf", "str-ll", "quickload"]
    datasets = [("osm", OSM_ROUTES)]  # TODO Geolife
    for name, (entries, entries_path) in datasets:
        for algorithm in algorithms:
            print("dataset = {}, algorithm = {}".format(name, algorithm))

            result = build_tree(algorithm, entries_path)
            result["dataset"] = name
            result["algorithm"] = algorithm
            results.append(result)

    return results

with (OUTPUT_PATH / "build_nodes.log").open("w") as logfile:
    def random_walk_table():
        eval_naive = run_random_walk(naive_node_building=True, logfile=logfile)
        eval_bulk = run_random_walk(naive_node_building=False, logfile=logfile)

        table = PrettyTable([
            "Type", "Labels", "I/O",
            "Duration", "Index Size", "Tree Size"]
        )
        table.align["Type"] = "l"
        table.align["Labels"] = "r"
        table.align["I/O"] = "r"
        table.align["Duration"] = "r"
        table.align["Index Size"] = "r"
        table.align["Tree Size"] = "r"

        def add_result(type, result):
            table.add_row([
                type,
                result["labels"],
                result["total_io"],
                result["duration"],
                format.bytes(result["index_size"]),
                format.bytes(result["tree_size"]),
            ])

        for result in eval_naive:
            add_result("naive", result)
        for result in eval_bulk:
            add_result("bulk", result)

        return table

    def others_table():
        eval_naive = run_others(naive_node_building=True, logfile=logfile)
        eval_bulk = run_others(naive_node_building=False, logfile=logfile)

        table = PrettyTable([
            "Type", "Dataset", "Algorithm", "I/O", "Duration"
        ])
        table.align["I/O"] = "r"
        table.align["Duration"] = "r"

        def add_result(type, result):
            table.add_row([
                type,
                result["dataset"],
                result["algorithm"],
                result["total_io"],
                result["duration"],
            ])

        for result in eval_naive:
            add_result("naive", result)
        for result in eval_bulk:
            add_result("bulk", result)

        return table

    t1 = random_walk_table()
    t2 = others_table()
    with (RESULT_PATH / "build_nodes.txt").open("w") as outfile:
        print("Random walk dataset with increasing number of labels.\n", file=outfile)
        print(t1, file=outfile)
        print("")

        print("Other datasets with different variating algorithms.\n", file=outfile)
        print(t2, file=outfile)
