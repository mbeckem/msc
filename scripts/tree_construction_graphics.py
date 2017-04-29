#!/usr/bin/env python3

from collections import defaultdict
import operator as o
import json
import numpy as np
import matplotlib
import matplotlib.cm as cm
import matplotlib.pyplot as plt

from common import OUTPUT_PATH, RESULT_PATH


def load_results():
    with (RESULT_PATH / "tree_building.json").open("r") as file:
        results = json.load(file)

    # Group by dataset -> algorithm -> entries
    datasets = defaultdict(lambda: defaultdict(list))
    for result in results:
        dataset = result["dataset"]
        algorithm = result["algorithm"]

        datasets[dataset][algorithm].append(result)
    return datasets

results = load_results()


def get_sequence(dataset, algorithm):
    return results[dataset][algorithm]


def plot_tree_construction(logscale, output_path):
    fig, (axis) = plt.subplots(3, 2, figsize=(14, 8))

    datasets = ["geolife", "osm", "random-walk"]
    algorithms = ["hilbert", "str-lf", "quickload", "obo"]
    markers = ["*", "o", "^", "+"]

    def plot(ax, dataset, key, ylabel, title):
        for algorithm, marker in zip(algorithms, markers):
            seq = get_sequence(dataset, algorithm)
            x = [r["entries"] for r in seq]
            y = [r[key] for r in seq]
            ax.plot(x, y, marker=marker, label=algorithm, linestyle="dashed")

        if logscale:
            ax.set_yscale('log')
        else:
            maxy = max([max(r[key] for r in get_sequence(dataset, algorithm))
                        for algorithm in algorithms if algorithm != "obo"])
            ax.set_ylim(bottom=0, top=maxy * 2)

        ax.set_xlabel("Items")
        ax.set_ylabel(ylabel)
        ax.set_title(title)

        ax.legend(loc='best', fancybox=True, ncol=2)

    plot(axis[0, 0], "geolife", "total_io",
         "IO Operations", "Geolife (IO)")
    plot(axis[0, 1], "geolife", "duration",
         "Seconds", "Geolife (Duration)")
    plot(axis[1, 0], "osm", "total_io",
         "IO Operations", "osm (IO)")
    plot(axis[1, 1], "osm", "duration",
         "Seconds", "osm (Duration)")
    plot(axis[2, 0], "random-walk", "total_io",
         "IO Operations", "Random walk (IO)")
    plot(axis[2, 1], "random-walk", "duration",
         "Seconds", "Random walk (Duration)")

    fig.tight_layout()
    fig.suptitle("Building a tree from stratch with different algorithms.")
    fig.subplots_adjust(top=0.93)
    fig.savefig(str(output_path), bbox_inches="tight")


plot_tree_construction(True, RESULT_PATH / "construction_logscale.pdf")
plot_tree_construction(False, RESULT_PATH / "construction.pdf")
