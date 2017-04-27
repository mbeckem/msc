#!/usr/bin/env python3
# Compares the naive and bulk node building algorithms.
# This requires a large amount of labels (otherwise, 1-by-1 insertion
# of index entries would be good enough).


import collections
import json

import common
import commands

from common import DATA_PATH, TMP_PATH, OUTPUT_PATH, RESULT_PATH, LOADER
from compile import compile
from datasets import RANDOM_WALK_VARYING_LABELS, OSM_ROUTES, GEOLIFE
from lib.prettytable import PrettyTable


# Get the specified keys in the dictionary as a list.
def keys(d, *args):
    result = []
    for key in args:
        result.append(d[key])
    return result


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
    for algorithm in ["hilbert", "str-lf", "quickload"]:
        for entries, labels, entries_path in RANDOM_WALK_VARYING_LABELS:
            print("random walk: algorithm = {}, entries = {}, labels = {}"
                  .format(algorithm, entries, labels))
            result = build_tree(algorithm, entries_path)
            result["type"] = "naive" if naive_node_building else "bulk"
            result["dataset"] = "random-walk"
            result["labels"] = labels
            results.append(result)
    return results


def run_others(naive_node_building, logfile):
    compile(naive_node_building=naive_node_building)

    results = []
    algorithms = ["hilbert", "str-plain", "str-lf", "str-ll", "quickload"]
    datasets = [("osm", OSM_ROUTES), ("geolife", GEOLIFE)]
    for name, (entries, entries_path) in datasets:
        for algorithm in algorithms:
            print("dataset = {}, algorithm = {}".format(name, algorithm))

            result = build_tree(algorithm, entries_path)
            result["type"] = "naive" if naive_node_building else "bulk"
            result["dataset"] = name
            results.append(result)

    return results

with (OUTPUT_PATH / "node_building.log").open("w") as logfile:
    random_walk_naive = run_random_walk(
        naive_node_building=True, logfile=logfile)
    others_naive = run_others(naive_node_building=True, logfile=logfile)
    random_walk_bulk = run_random_walk(
        naive_node_building=False, logfile=logfile)
    others_bulk = run_others(naive_node_building=False, logfile=logfile)

    random_walk = random_walk_naive + random_walk_bulk
    others = others_naive + others_bulk

with (RESULT_PATH / "node_building_random_walk.txt").open("w") as tablefile:
    table = PrettyTable([
        "Type", "Algorithm", "Labels", "I/O",
        "Duration", "Index Size", "Tree Size"
    ])
    table.align["Labels"] = "r"
    table.align["I/O"] = "r"
    table.align["Duration"] = "r"
    table.align["Index Size"] = "r"
    table.align["Tree Size"] = "r"

    for result in random_walk:
        table.add_row(
            keys(result,
                 "type", "algorithm", "labels",
                 "total_io", "duration",
                 "index_size", "tree_size"))

    print(table, file=tablefile)

with (RESULT_PATH / "node_building_others.txt").open("w") as tablefile:
    table = PrettyTable([
        "Dataset", "Type", "Algorithm", "I/O", "Duration"
    ])
    table.align["I/O"] = "r"
    table.align["Duration"] = "r"

    for result in others:
        table.add_row(
            keys(result,
                 "dataset", "type", "algorithm",
                 "total_io", "duration"))

    print(table, file=tablefile)

with (RESULT_PATH / "node_building_random_walk.json").open("w") as outfile:
    json.dump(random_walk, outfile, sort_keys=True, indent=4)

with (RESULT_PATH / "node_building_others.json").open("w") as outfile:
    json.dump(others, outfile, sort_keys=True, indent=4)
