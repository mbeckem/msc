#!/usr/bin/env python3
# Generates hilbert curve images.

import json
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from mpl_toolkits.mplot3d import Axes3D
import numpy as np
import subprocess

from common import HILBERT_CURVE, RESULT_PATH

curves = json.loads(subprocess.check_output(
    str(HILBERT_CURVE)).decode("utf-8"))


def get_curve(dimension, precision):
    for curve in curves:
        if curve["dimension"] == dimension and curve["precision"] == precision:
            points = curve["points"]
            return np.array(points)
    raise KeyError("No curve with dimension {} and precision {}"
                   .format(dimension, precision))


def plot2d(fig, gridpos, precision, title):
    MAX = 2 ** precision - 1

    ax = fig.add_subplot(gridpos)
    points = get_curve(2, precision)

    # Every x,y coordiante (excluding the last point)
    X = points[:-1, 0]
    Y = points[:-1, 1]

    # Vector direction x & y
    U = points[1:, 0] - X
    V = points[1:, 1] - Y

    cmap = matplotlib.cm.get_cmap("gist_rainbow")
    C = np.linspace(0, 1, len(X))
    C = np.concatenate((C, np.repeat(C, 2)))

    ax.quiver(X, Y, U, V, color=cmap(C), units="xy", scale=1)
    ax.locator_params(integer=True)
    ax.axis("equal")
    ax.set_title(title)
    ax.set_xticks([])
    ax.set_yticks([])
    ax.set_xticklabels([])
    ax.set_yticklabels([])


def plot3d(fig, gridpos, precision, title):
    MAX = 2 ** precision - 1

    ax = fig.add_subplot(gridpos, projection="3d")
    points = get_curve(3, precision)

    X = points[:-1, 0]
    Y = points[:-1, 1]
    Z = points[:-1, 2]

    U = points[1:, 0] - X
    V = points[1:, 1] - Y
    W = points[1:, 2] - Z

    cmap = matplotlib.cm.get_cmap("gist_rainbow")

    # Jesus.
    # Quiver draws all arrow quivers first,
    # then draws the two arrow head lines at the same time.
    # Thus, the arrow should look like this:
    # [0, ..., 1, 0, 0, ..., 1, 1]
    # Of course, none of this was documented.
    C = np.linspace(0, 1, len(X))
    C = np.concatenate((C, np.repeat(C, 2)))

    ax.quiver(X, Y, Z, U, V, W, color=cmap(C), length=1)
    ax.locator_params(integer=True)

    ax.set_xticks([])
    ax.set_yticks([])
    ax.set_zticks([])
    ax.set_xticklabels([])
    ax.set_yticklabels([])
    ax.set_zticklabels([])

    ax.set_xlim(0, MAX)
    ax.set_ylim(0, MAX)
    ax.set_zlim(0, MAX)
    ax.set_title(title)

fig = plt.figure(figsize=(12, 8))
g = gridspec.GridSpec(2, 3)
g.update(wspace=0.05, hspace=0.05)

plot2d(fig, g[0], 1, "a)")
plot2d(fig, g[1], 2, "b)")
plot2d(fig, g[2], 3, "c)")
plot3d(fig, g[3], 1, "d)")
plot3d(fig, g[4], 2, "e)")
plot3d(fig, g[5], 3, "f)")
fig.savefig(str(RESULT_PATH / "hilbert_curves.pdf"), bbox_inches="tight")
