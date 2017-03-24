#!/usr/bin/env python3

import json
import matplotlib
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

import numpy as np
import subprocess

from commands import HILBERT_LEAVES, RESULT_PATH


def get_leaves(points, leaf_size, skewed=False, seed=None, heuristic=False):
    fn_args = locals()

    args = [
        str(HILBERT_LEAVES),
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


def draw_leaves(ax, leaves):
    ax.set_title("Leaves")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)

    for leaf in leaves:
        mbb = leaf["mbb"]
        x = mbb["min"]["x"]
        y = mbb["min"]["y"]
        width = mbb["max"]["x"] - x
        height = mbb["max"]["y"] - y
        ax.add_patch(Rectangle(xy=(x, y), width=width,
                               height=height, fill=False))


fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(12, 4))
draw_leaves(ax1, get_leaves(1000, 16, seed=1380799046))
draw_leaves(ax2, get_leaves(1000, 16, skewed=True, seed=4161942469))
draw_leaves(ax3, get_leaves(1000, 16, skewed=True,
                            seed=4161942469, heuristic=True))


fig.savefig(str(RESULT_PATH / "hilbert_leaves.pdf"), bbox_inches="tight")
plt.show()
