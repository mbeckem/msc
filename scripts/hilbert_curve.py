#!/usr/bin/env python3

import json
import matplotlib
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import numpy as np
import subprocess

from commands import HILBERT_CURVE, RESULT_PATH

curves = json.loads(subprocess.check_output(
    str(HILBERT_CURVE)).decode("utf-8"))


def get_curve(dimension, precision):
    for curve in curves:
        if curve["dimension"] == dimension and curve["precision"] == precision:
            points = curve["points"]
            return np.array(points)
    raise KeyError("No curve with dimension {} and precision {}"
                   .format(dimension, precision))


def plot2d(fig, id, precision, title):
    MAX = 2 ** precision - 1

    ax = fig.add_subplot(id)
    points = get_curve(2, precision)

    # Every x,y coordiante (excluding the last point)
    X = points[:-1, 0]
    Y = points[:-1, 1]

    # Vector direction x & y
    U = points[1:, 0] - X
    V = points[1:, 1] - Y

    ax.quiver(X, Y, U, V, color="black", units="xy", scale=1)
    ax.locator_params(integer=True)
    ax.axis("equal")

    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title(title)


def plot3d(fig, id, precision, title):
    MAX = 2 ** precision - 1

    ax = fig.add_subplot(id, projection="3d")
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

    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_zlabel("z")
    ax.set_xlim(0, MAX)
    ax.set_ylim(0, MAX)
    ax.set_zlim(0, MAX)
    ax.set_title(title)


fig = plt.figure(figsize=(16, 16))
plot2d(fig, 221, 1, "2D, Precision = 1")
plot2d(fig, 222, 2, "2D, Precision = 2")
plot3d(fig, 223, 1, "3D, Precision = 1")
plot3d(fig, 224, 2, "3D, Precision = 2")
fig.savefig(str(RESULT_PATH / "hilbert_curves.pdf"), bbox_inches="tight")
