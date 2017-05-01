#!/usr/bin/env python3

import operator as o
import json
import numpy as np
import matplotlib
import matplotlib.cm as cm
import matplotlib.pyplot as plt
from colorsys import hls_to_rgb

import common
from common import OUTPUT_PATH, RESULT_PATH


def ordered_unique(seq):
    s = set()
    result = []
    for i in seq:
        if i not in s:
            result.append(i)
            s.add(i)
    return result


# http://stackoverflow.com/a/4382138/4928144
kelly_colors_hex = [
    0xFFB300,  # Vivid Yellow
    0x803E75,  # Strong Purple
    0xFF6800,  # Vivid Orange
    0xA6BDD7,  # Very Light Blue
    0xC10020,  # Vivid Red
    0xCEA262,  # Grayish Yellow
    0x817066,  # Medium Gray

    # The following don't work well for people with defective color vision
    0x007D34,  # Vivid Green
    0xF6768E,  # Strong Purplish Pink
    0x00538A,  # Strong Blue
    0xFF7A5C,  # Strong Yellowish Pink
    0x53377A,  # Strong Violet
    0xFF8E00,  # Vivid Orange Yellow
    0xB32851,  # Strong Purplish Red
    0xF4C800,  # Vivid Greenish Yellow
    0x7F180D,  # Strong Reddish Brown
    0x93AA00,  # Vivid Yellowish Green
    0x593315,  # Deep Yellowish Brown
    0xF13A13,  # Vivid Reddish Orange
    0x232C16,  # Dark Olive Green
]
kelly_colors = [
    (((v >> 16) & 255) / 255,
     ((v >> 8) & 255) / 255,
     ((v >> 0) & 255) / 255) for v in kelly_colors_hex
]


# Taken from
# http://emptypipes.org/2013/11/09/matplotlib-multicategory-barchart/
def barplot(ax, dpoints):
    '''
    Create a barchart for data across different categories with
    multiple algorithms for each category.

    @param ax: The plotting axes from matplotlib.
    @param dpoints: The data set as an (n, 3) numpy array
    '''

    # Aggregate the algorithms and the categories according to their
    # mean values

    # sort the algorithms, categories and data so that the bars in
    # the plot will be ordered by category and condition
    categories = ordered_unique(dpoints[:, 0])
    algorithms = ordered_unique(dpoints[:, 1])

    # the space between each set of bars
    space = 0.3
    n = len(algorithms)
    width = (1 - space) / (len(algorithms))

    # Create a set of bars at each position
    for i, cond in enumerate(algorithms):
        indeces = range(1, len(categories) + 1)
        vals = dpoints[dpoints[:, 1] == cond][:, 2].astype(np.float)
        pos = [j - (1 - space) / 2. + i * width for j in indeces]
        ax.bar(pos, vals, width=width, label=cond,
               color=kelly_colors[i])

    tickpos = [j - (1 - space) / 2. + (len(algorithms) / 2)
               * width - (width / 2) for j in indeces]
    ax.set_xticks(tickpos)
    ax.set_xticklabels(categories)
    ax.xaxis.set_ticks_position('none')
    # plt.setp(plt.xticks()[1], rotation=90)

    # Add the axis labels
    ax.set_ylabel("Average IO Operations")

    # Add a legend
    handles, labels = ax.get_legend_handles_labels()
    ax.legend(reversed(handles[::-1]),
              reversed(labels[::-1]), loc='best')


def load_results():
    with (RESULT_PATH / "queries.json").open("r") as file:
        return json.load(file)

# 1 entry for each dataset.
# Within each dataset, one entry for each tree.
# Within each tree, one entry for every query category (small, large, sequenced).
# Which in turn have an average number of block io operations (and more
# specific stats per query).
#
# Dataset -> Tree -> Query Set -> [avg | Query]
results = load_results()


def tree_result(dataset, tree, query_set):
    return results[dataset][tree][query_set]["avg"]


# 3 axes, one for every query set
def plot_querysets(axes, datasets):
    for query_set, ax in zip(["small", "large", "sequenced"], axes):
        datapoints = []
        for (dataset_title, dataset_name), tree_set in datasets:
            for tree_title, tree_name in tree_set:
                datapoints.append([
                    dataset_title,
                    tree_title,
                    tree_result(dataset_name, tree_name, query_set)
                ])
        barplot(ax, np.array(datapoints))
        ax.set_title("Queryset {}".format(query_set))


def plot_main_algorithms(output_path):
    fig, axes = plt.subplots(1, 3, figsize=(14, 5))

    datasets = [
        (("Geolife", "geolife"), [
            ("hilbert", "geolife-hilbert"),
            ("str-lf", "geolife-str-lf"),
            ("quickload", "geolife-quickload"),
            ("obo", "geolife-obo")
        ]),
        (("OpenStreetMaps", "osm"), [
            ("hilbert", "osm-hilbert"),
            ("str-lf", "osm-str-lf"),
            ("quickload", "osm-quickload"),
            ("obo", "osm-obo")
        ]),
        (("Random-Walk", "random-walk"), [
            ("hilbert", "random-walk-hilbert"),
            ("str-lf", "random-walk-str-lf"),
            ("quickload", "random-walk-quickload"),
            ("obo", "random-walk-obo")
        ]),
    ]
    plot_querysets(axes, datasets)

    fig.tight_layout()
    fig.suptitle("Query-Performance von Bäumen pro Algorithmus.")
    fig.subplots_adjust(top=0.85)
    fig.savefig(str(output_path), bbox_inches="tight")


def plot_beta_values(output_path):
    fig, axes = plt.subplots(3, 1, figsize=(8, 12))

    datasets = [
        (("Geolife", "geolife"), [
            ("$\\beta=0.25$", "geolife-obo-beta-0.25"),
            ("$\\beta=0.5$", "geolife-obo"),
            ("$\\beta=0.75$", "geolife-obo-beta-0.25"),
            ("$\\beta=1$", "geolife-obo-beta-1.0")
        ]),
        (("Geolife (quickload)", "geolife"), [
            ("$\\beta=0.25$", "geolife-quickload-beta-0.25"),
            ("$\\beta=0.5$", "geolife-quickload"),
            ("$\\beta=0.75$", "geolife-quickload-beta-0.25"),
            ("$\\beta=1$", "geolife-quickload-beta-1.0")
        ]),
        (("OSM", "osm"), [
            ("$\\beta=0.25$", "osm-obo-beta-0.25"),
            ("$\\beta=0.5$", "osm-obo"),
            ("$\\beta=0.75$", "osm-obo-beta-0.25"),
            ("$\\beta=1$", "osm-obo-beta-1.0")
        ]),
        (("OSM (quickload)", "osm"), [
            ("$\\beta=0.25$", "osm-quickload-beta-0.25"),
            ("$\\beta=0.5$", "osm-quickload"),
            ("$\\beta=0.75$", "osm-quickload-beta-0.25"),
            ("$\\beta=1$", "osm-quickload-beta-1.0")
        ]),
    ]
    plot_querysets(axes, datasets)

    fig.tight_layout()
    fig.suptitle("Verschiedene Werte für $\\beta$")
    fig.subplots_adjust(top=0.93)
    fig.savefig(str(output_path), bbox_inches="tight")


def plot_beta_strategies(output_path):
    fig, axes = plt.subplots(3, 1, figsize=(8, 12))

    datasets = [
        (("Geolife", "geolife"), [
            ("normal", "geolife-obo"),
            ("increasing", "geolife-obo-beta-increasing"),
            ("decreasing", "geolife-obo-beta-decreasing"),
        ]),
        (("Geolife (quickload)", "geolife"), [
            ("normal", "geolife-quickload"),
            ("increasing", "geolife-quickload-beta-increasing"),
            ("decreasing", "geolife-quickload-beta-decreasing"),
        ]),
        (("OSM", "osm"), [
            ("normal", "osm-obo"),
            ("increasing", "osm-obo-beta-increasing"),
            ("decreasing", "osm-obo-beta-decreasing"),
        ]),
        (("OSM (quickload)", "osm"), [
            ("normal", "osm-quickload"),
            ("increasing", "osm-quickload-beta-increasing"),
            ("decreasing", "osm-quickload-beta-decreasing"),
        ]),
    ]
    plot_querysets(axes, datasets)

    fig.tight_layout()
    fig.suptitle("Verschiedene Strategien für $\\beta$")
    fig.subplots_adjust(top=0.93)
    fig.savefig(str(output_path), bbox_inches="tight")


def plot_str_variants(output_path):
    fig, axes = plt.subplots(1, 3, figsize=(12, 5))

    datasets = [
        (("Geolife", "geolife"), [
            ("str-plain", "geolife-str-plain"),
            ("str-lf", "geolife-str-lf"),
            ("str-ll", "geolife-str-ll"),
        ]),
        (("OSM", "osm"), [
            ("str-plain", "osm-str-plain"),
            ("str-lf", "osm-str-lf"),
            ("str-ll", "osm-str-ll"),
        ]),
    ]
    plot_querysets(axes, datasets)

    fig.tight_layout()
    fig.suptitle("Verschiedene Varianten von STR")
    fig.subplots_adjust(top=0.85)
    fig.savefig(str(output_path), bbox_inches="tight")


def plot_bloom_filters(output_path):
    fig, ax = plt.subplots(1, 1, figsize=(5, 4))

    datasets = [
        (("Geolife", "geolife"), [
            ("interval sets", "geolife-quickload"),
            ("interval sets (rnd. ids)", "geolife-shuffled-quickload"),
            ("bloom filters", "geolife-quickload-bloom"),
        ]),
        (("OSM", "osm"), [
            ("interval sets", "osm-quickload"),
            ("interval sets (rnd. ids)", "osm-shuffled-quickload"),
            ("bloom filters", "osm-quickload-bloom"),
        ]),
    ]
    datapoints = []
    for (dataset_title, dataset_name), tree_set in datasets:
        for tree_title, tree_name in tree_set:
            datapoints.append([
                dataset_title,
                tree_title,
                tree_result(dataset_name, tree_name, "sequenced")
            ])
    barplot(ax, np.array(datapoints))
    ax.set_title("Queryset {}".format("sequenced"))

    fig.tight_layout()
    fig.suptitle("Vergleich von Intervall-Set und Bloom-Filter (Quickload)")
    fig.subplots_adjust(top=0.85)
    fig.savefig(str(output_path), bbox_inches="tight")


def plot_fanout(output_path):
    def avg_result(dataset, tree):
        querysets = ["small", "large", "sequenced"]
        s = sum(tree_result(dataset, tree, queryset)
                for queryset in querysets)
        return s / len(querysets)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9, 4))

    trees = ["geolife-quickload-fanout-32",
             "geolife-quickload-fanout-50",
             "geolife-quickload-fanout-64",
             "geolife-quickload"]

    querysets = ["small", "large", "sequenced"]
    markers = ["o", "^", "*"]
    x = [32, 50, 64, 113]
    for queryset, marker in zip(querysets, markers):
        y = [tree_result("geolife", tree, queryset) for tree in trees]
        ax1.plot(x, y, label=queryset, marker=marker, linestyle="dashed")

    ax1.set_title("Query Performance.")
    ax1.set_xticks(x)
    ax1.set_xlabel("Fan-out")
    ax1.set_ylabel("Average IO Operations")
    ax1.legend(loc="best")

    tree_paths = [
        OUTPUT_PATH / "variants" / "geolife-quickload-fanout-{}".format(fanout)
        for fanout in [32, 50, 64]
    ]
    tree_paths.append(OUTPUT_PATH / "geolife-quickload")

    total_size = [common.file_size(p) / 2 ** 20 for p in tree_paths]
    rtree_size = [common.file_size(
        p / "tree.blocks") / 2 ** 20 for p in tree_paths]
    index_size = [ts - rs for ts, rs in zip(total_size, rtree_size)]

    ax2.plot(x, rtree_size, marker="o", linestyle="dashed", label="R-Baum")
    ax2.plot(x, index_size, marker="^", linestyle="dashed", label="Index")

    ax2.set_title("Größe des Baums.")
    ax2.set_xticks(x)
    ax2.set_xlabel("Fan-out")
    ax2.set_ylabel("Megabyte")
    ax2.legend(loc="best")

    fig.tight_layout()
    fig.suptitle("Performance mit variierendem Fan-out. Quickload / Geolife.")
    fig.subplots_adjust(top=0.88)
    fig.savefig(str(output_path), bbox_inches="tight")


plot_main_algorithms(RESULT_PATH / "query.pdf")
plot_beta_values(RESULT_PATH / "query_beta_values.pdf")
plot_beta_strategies(RESULT_PATH / "query_beta_strategies.pdf")
plot_str_variants(RESULT_PATH / "query_str_variants.pdf")
plot_bloom_filters(RESULT_PATH / "query_bloom_filters.pdf")
plot_fanout(RESULT_PATH / "query_fanout.pdf")
