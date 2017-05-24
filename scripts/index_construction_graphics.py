#!/usr/bin/env python3
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
    with (RESULT_PATH / "node_building_random_walk.json").open("r") as file:
        results = json.load(file)

    # Group by type -> algorithm -> label.
    # Data set is only random walk.
    datasets = defaultdict(lambda: defaultdict(list))
    for result in results:
        type = result["type"]
        algorithm = result["algorithm"]
        datasets[type][algorithm].append(result)
    return datasets

results = load_results()


def get_sequence(type, algorithm):
    return results[type][algorithm]


def plot_index_construction(output_path):
    fig, axes = plt.subplots(1, 3, figsize=(12, 4))

    types = ["bulk", "naive"]
    algorithms = ["hilbert", "str-lf", "quickload"]
    markers = ["*", "o", "^"]

    def plot(ax, key, ylabel, title):
        for type in types:
            for algorithm, marker in zip(algorithms, markers):
                seq = get_sequence(type, algorithm)
                x = [r["labels"] for r in seq]
                y = [r[key] for r in seq]
                ax.plot(x, y, marker=marker,
                        label=algorithm if type == "bulk" else "{} (naiv)".format(
                            algorithm),
                        linestyle="dashed" if type == "bulk" else "dotted")

        ax.set_xlabel("Labels")
        ax.set_ylabel(ylabel)
        ax.set_title(title)

    def plot_index_size(ax):
        for algorithm, marker in zip(algorithms, markers):
            seq = get_sequence("bulk", algorithm)
            x = [r["labels"] for r in seq]
            y = [r["index_size"] / 2**20 for r in seq]
            ax.plot(x, y, marker=marker, label="{}".format(algorithm),
                    linestyle="dashed")

        ax.set_xlabel("Labels")
        ax.set_ylabel("Megabyte")
        ax.set_title("Größe des invertierten Index")

    plot(axes[0], "total_io", "I/Os",
         "Konstruktionskosten (I/O)")
    plot(axes[1], "duration", "Sekunden",
         "Konstruktionskosten (Zeit)")
    plot_index_size(axes[2])

    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", bbox_to_anchor=(0.5, 0),
               bbox_transform=fig.transFigure, ncol=2)

    fig.tight_layout()
    # fig.suptitle(
    #    "Build tree from random-walk dataset "
    #    " (2000000 items) "
    #    "with varying number of labels.")
    # fig.subplots_adjust(top=0.93)
    fig.savefig(str(output_path), bbox_inches="tight")


plot_index_construction(RESULT_PATH / "index_construction.pdf")
