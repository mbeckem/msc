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
    fig, axis = plt.subplots(3, 2, figsize=(10, 10))

    datasets = ["geolife", "osm", "random-walk"]
    algorithms = ["hilbert", "str-lf", "quickload", "obo"]
    markers = ["*", "o", "^", "+"]

    def plot(ax, dataset, key, ylabel, title):
        ax.locator_params(nbins=4, min_n_ticks=4, axis='x')

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

        ax.set_xlabel("Einträge")
        ax.set_ylabel(ylabel)
        ax.set_title(title)
        ax.get_xaxis().get_major_formatter().set_scientific(False)
        ax.legend(loc='best', fancybox=True, ncol=2)

    plot(axis[0, 0], "geolife", "total_io",
         "I/Os", "geolife (I/O)")
    plot(axis[0, 1], "geolife", "duration",
         "Sekunden", "geolife (Zeit)")
    plot(axis[1, 0], "osm", "total_io",
         "I/Os", "osm (I/O)")
    plot(axis[1, 1], "osm", "duration",
         "Sekunden", "osm (Zeit)")
    plot(axis[2, 0], "random-walk", "total_io",
         "I/Os", "random-walk (I/O)")
    plot(axis[2, 1], "random-walk", "duration",
         "Sekunden", "random-walk (Zeit)")

    fig.tight_layout()
    fig.savefig(str(output_path), bbox_inches="tight")


def plot_fanout_construction(output_path):
    with (RESULT_PATH / "fanouts.json").open() as f:
        fanout_results = json.load(f)

    algorithms = ["hilbert", "str-lf", "quickload"]  # , obo (außer konkurrenz)
    markers = ["*", "o", "^", "+"]

    def plot(ax, key, ylabel, title):
        for algorithm, marker in zip(algorithms, markers):
            seq = [fanout_results[str(f)][algorithm][0]
                   for f in [32, 50, 64, 0]]
            x = [32, 50, 64, 113]
            y = [s[key] for s in seq]
            ax.plot(x, y, marker=marker, label=algorithm, linestyle="dashed")
        ax.set_xticks([32, 50, 64, 113])
        ax.set_xlabel("Fan-out")
        ax.set_ylabel(ylabel)
        ax.set_title(title)
        ax.legend(loc="best", fancybox=True, ncol=1)

    fig, axis = plt.subplots(1, 2, figsize=(10, 4))
    plot(axis[0], "total_io", "I/Os", "Konstruktionkosten (I/O)")
    plot(axis[1], "duration", "Sekunden", "Konstruktionskosten (Zeit)")

    fig.tight_layout()
    fig.savefig(str(output_path), bbox_inches="tight")


plot_tree_construction(True, RESULT_PATH / "construction_logscale.pdf")
plot_tree_construction(False, RESULT_PATH / "construction.pdf")
plot_fanout_construction(RESULT_PATH / "construction_fanout.pdf")
