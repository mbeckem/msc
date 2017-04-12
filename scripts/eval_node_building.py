#!/usr/bin/env python3

import collections
import json
import shutil
import subprocess
from pathlib import Path

import commands
import format
from commands import DATA_PATH, TMP_PATH, OUTPUT_PATH, RESULT_PATH, LOADER
from compile import compile
from lib.prettytable import PrettyTable


def load_tree(tree_path, entries_path, stats_path, logfile):
    subprocess.check_call([
        str(commands.LOADER),
        "--algorithm", "hilbert",
        "--entries", str(entries_path),
        "--tree", str(tree_path),
        "--stats", str(stats_path)
    ], stdout=logfile)

dataset = [
    (10,    DATA_PATH / "walk-generated-n5000000-l10.entries"),
    (20,    DATA_PATH / "walk-generated-n5000000-l20.entries"),
    (30,    DATA_PATH / "walk-generated-n5000000-l30.entries"),
    (40,    DATA_PATH / "walk-generated-n5000000-l40.entries"),
    (50,    DATA_PATH / "walk-generated-n5000000-l50.entries"),
    (100,   DATA_PATH / "walk-generated-n5000000-l100.entries"),
    (150,   DATA_PATH / "walk-generated-n5000000-l150.entries"),
    (200,   DATA_PATH / "walk-generated-n5000000-l200.entries"),
    (250,   DATA_PATH / "walk-generated-n5000000-l250.entries"),
    (500,   DATA_PATH / "walk-generated-n5000000-l500.entries"),
    (1000,  DATA_PATH / "walk-generated-n5000000-l1000.entries"),
    (2000,  DATA_PATH / "walk-generated-n5000000-l2000.entries"),
    (5000,  DATA_PATH / "walk-generated-n5000000-l5000.entries"),
    (10000, DATA_PATH / "walk-generated-n5000000-l10000.entries"),
]


Result = collections.namedtuple(
    "Result", ["labels", "read_io", "write_io", "total_io", "duration", "index_size", "tree_size", "total_size"])


tree_path = TMP_PATH / "node_building_tmp_tree"


def run(naive_node_building, logfile):
    compile(naive_node_building=naive_node_building)

    results = []
    for labels, entries_path in dataset:
        print("labels = {}, entries = {}".format(labels, entries_path))

        stats = OUTPUT_PATH / "node_building_{}_{}.json".format(
            "naive" if naive_node_building else "bulk", labels)

        commands.remove(tree_path)
        load_tree(tree_path, entries_path, stats, logfile)
        print("\n\n", file=logfile, flush=True)

        stats_json = None
        with stats.open() as f:
            stats_json = json.load(f)

        total_size = commands.file_size(tree_path)
        tree_size = commands.file_size(tree_path / "tree.blocks")
        index_size = total_size - tree_size

        result = Result(labels=labels,
                        read_io=stats_json["read_io"],
                        write_io=stats_json["write_io"],
                        total_io=stats_json["total_io"],
                        duration=stats_json["duration"],
                        tree_size=tree_size,
                        index_size=index_size,
                        total_size=total_size)
        results.append(result)
    return results

with (OUTPUT_PATH / "node_building.log").open("w") as logfile, \
        (RESULT_PATH / "node_building.txt").open("w") as outfile:

    eval_naive = run(naive_node_building=True, logfile=logfile)
    eval_bulk = run(naive_node_building=False, logfile=logfile)

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
            result.labels,
            result.total_io,
            result.duration,
            format.bytes(result.index_size),
            format.bytes(result.tree_size),
        ])

    for result in eval_naive:
        add_result("naive", result)
    for result in eval_bulk:
        add_result("bulk", result)

    print(table, file=outfile)
