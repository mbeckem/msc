#!/usr/bin/env python3

import json
import matplotlib
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

import numpy as np
import subprocess

from common import ALGORITHM_EXAMPLES, RESULT_PATH
from compile import compile


def get_leaves(points, leaf_size, algorithm, skewed=False, seed=None, heuristic=False):
    fn_args = locals()

    args = [
        str(ALGORITHM_EXAMPLES),
        "--algorithm", algorithm,
        "--points", str(points),
        "--leaf-size", str(leaf_size),
    ]
    if skewed:
        args.append("--skewed")
    if heuristic:
        args.append("--heuristic")
    if seed is not None:
        args.extend(["--seed", str(seed)])

    result = json.loads(subprocess.check_output(args).decode("utf-8"))
    print("seed for args {} is {}".format(fn_args, result["seed"]))
    return result["leaves"]


def draw_leaves(ax, title, leaves):
    ax.set_title(title)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)

    cmap = matplotlib.cm.get_cmap("gist_rainbow")
    colors = cmap(np.linspace(0, 1, len(leaves)))

    for color, leaf in zip(colors, leaves):
        color[3] = 0.5

        mbb = leaf["mbb"]
        x = mbb["min"]["x"]
        y = mbb["min"]["y"]
        width = mbb["max"]["x"] - x
        height = mbb["max"]["y"] - y
        ax.add_patch(Rectangle(xy=(x, y), width=width,
                               height=height, fill=True, facecolor=color, edgecolor="black"))


def save(fig, path):
    fig.savefig(str(path), bbox_inches="tight")


def make_hilbert():
    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(12, 4))
    draw_leaves(ax1, "a)",
                get_leaves(1000, 16, algorithm="hilbert", seed=1380799046))
    draw_leaves(ax2, "b)",
                get_leaves(1000, 16, algorithm="hilbert",
                           skewed=True, seed=1821311943))
    draw_leaves(ax3, "c)",
                get_leaves(1000, 16, algorithm="hilbert", skewed=True,
                           seed=1821311943, heuristic=True))
    return fig


def make_str():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8, 4))
    draw_leaves(ax1, "a)",
                get_leaves(1000, 16, algorithm="str", seed=1380799046))
    draw_leaves(ax2, "b)",
                get_leaves(1000, 16, algorithm="str", skewed=True, seed=1821311943))
    return fig

compile()
save(make_hilbert(), RESULT_PATH / "hilbert_leaves.pdf")
save(make_str(), RESULT_PATH / "str_leaves.pdf")
