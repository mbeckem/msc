#!/usr/bin/env python3

import operator as o
import json
import numpy as np
import matplotlib
import matplotlib.cm as cm
import matplotlib.pyplot as plt

from common import OUTPUT_PATH, RESULT_PATH


def ordered_unique(seq):
    s = set()
    result = []
    for i in seq:
        if i not in s:
            result.append(i)
            s.add(i)
    return result


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
               color=cm.Accent(float(i) / n))

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
              reversed(labels[::-1]), loc='upper right')


def load_results():
    # FIXME
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
    fig, axes = plt.subplots(1, 3, figsize=(12, 5))

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
        ])
    ]
    plot_querysets(axes, datasets)

    fig.tight_layout()
    fig.suptitle("Verschiedene Anfragetypen")
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
    fig, ax = plt.subplots(1, 1, figsize=(4, 3))

    datasets = [
        (("Geolife", "geolife"), [
            ("interval sets", "geolife-quickload"),
            ("bloom filters", "geolife-quickload-bloom"),
        ]),
        (("OSM", "osm"), [
            ("interval sets", "osm-quickload"),
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
    fig.suptitle("Vergleich von Intervall-Sets und Bloom-Filter (Quickload)")
    fig.subplots_adjust(top=0.85)
    fig.savefig(str(output_path), bbox_inches="tight")

plot_main_algorithms(RESULT_PATH / "query.pdf")
plot_beta_values(RESULT_PATH / "query_beta_values.pdf")
plot_beta_strategies(RESULT_PATH / "query_beta_strategies.pdf")
plot_str_variants(RESULT_PATH / "query_str_variants.pdf")
plot_bloom_filters(RESULT_PATH / "query_bloom_filters.pdf")
