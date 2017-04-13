#!/usr/bin/env python3

import collections
import json
import shutil
import subprocess
from pathlib import Path

import common
import format
from common import DATA_PATH, TMP_PATH, OUTPUT_PATH, RESULT_PATH, LOADER
from compile import compile
from datasets import RANDOM_WALK_VARYING_LABELS
from lib.prettytable import PrettyTable


def load_tree(tree_path, entries_path, stats_path, logfile):
    subprocess.check_call([
        str(LOADER),
        "--algorithm", "hilbert",
        "--entries", str(entries_path),
        "--tree", str(tree_path),
        "--stats", str(stats_path)
    ], stdout=logfile)

Result = collections.namedtuple(
    "Result", ["labels", "read_io", "write_io", "total_io", "duration", "index_size", "tree_size", "total_size"])


def run(naive_node_building, logfile):
    compile(naive_node_building=naive_node_building)

    stats_dir = OUTPUT_PATH / "build_nodes_stats"

    results = []
    for entries, labels, entries_path in RANDOM_WALK_VARYING_LABELS:
        print("entries = {}, labels = {}, path = {}"
              .format(entries, labels, entries_path))

        stats = stats_dir / "{}_{}.json".format(
            "naive" if naive_node_building else "bulk", labels)

        tree_path = TMP_PATH / "node_building_tmp_tree"
        common.remove(tree_path)
        load_tree(tree_path, entries_path, stats, logfile)
        print("\n\n", file=logfile, flush=True)

        stats_json = None
        with stats.open() as f:
            stats_json = json.load(f)

        total_size = common.file_size(tree_path)
        tree_size = common.file_size(tree_path / "tree.blocks")
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

with (OUTPUT_PATH / "build_nodes.log").open("w") as logfile, \
        (RESULT_PATH / "build_nodes.txt").open("w") as outfile:

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
