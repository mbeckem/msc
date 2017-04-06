#!/usr/bin/env python3

import json
import matplotlib
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import numpy as np
import subprocess

from itertools import combinations, product
from commands import RESULT_PATH

# Format: min, max, color, linestyle, label, labelpos
cubes = [
    # N1
    ((13, 5, 0), (30, 20, 5), "black", "solid", "$R_1$", (12, 5, -1)),
    # N2
    ((4, 5, 5), (15, 18, 15), "black", "dashed", "$R_2$", (3, 5, 4)),
    # N3
    ((15, 10, 5), (25, 25, 15), "black", "dotted", "$R_3$", (26, 25, 15)),
    # N4
    ((5, 15, 10), (15, 25, 20), "black", "dashdot", "$R_4$", (4, 14, 20)),
]

trajectories = [
    {
        "points": [(30, 20, 0), (15, 18, 5), (4, 10, 10), (5, 5, 15)],
        "label": "Trajectory 1",
        "color": "red",
    },
    {
        "points": [(20, 5, 0), (25, 10, 5), (25, 20, 10), (15, 25, 15), (5, 25, 20)],
        "label": "Trajectory 2",
        "color": "green",
    },
    {
        "points": [(15, 9, 0), (13, 12, 5), (13, 15, 10), (10, 18, 15), (6, 17, 20)],
        "label": "Trajectory 3",
        "color": "blue",
    },
]


def pairwise(iterable):
    """iterate over adjecent pairs"""
    it = iter(iterable)
    a = next(it, None)
    for b in it:
        yield (a, b)
        a = b


def plot_cube(ax, min, max, color, linestyle, label, labelpos):
    # All cube corners
    points = np.array(list(product(*zip(min, max))))

    # All cube corner pairs
    for a, b in combinations(points, 2):
        # Differing coordinates
        diffs = (1 for i in range(3) if a[i] != b[i])
        # Draw a line if they differ in exactly one coordinate
        if sum(diffs) == 1:
            #‘solid’ | ‘dashed’, ‘dashdot’, ‘dotted’ | (offset, on-off-dash-seq)
            ax.plot3D(*zip(a, b), color=color, linestyle=linestyle)

    ax.text(*labelpos, label, color=color)


def plot_trajectory(ax, points, color, label):
    ax.plot3D(*zip(*points), color=color, marker="o", label=label)

fig = plt.figure(figsize=(8, 8))

ax = fig.add_subplot(111, projection="3d")
ax.set_xlabel("x")
ax.set_ylabel("y")
ax.set_zlabel("t")
ax.set_aspect("equal")
ax.locator_params(integer=True)

for traj in trajectories:
    plot_trajectory(ax, traj["points"], traj["color"], traj["label"])

for cube in cubes:
    plot_cube(ax, *cube)

ax.set_xlim(0)
ax.set_xticks([0, 10, 20, 30])
ax.set_ylim(0)
ax.set_yticks([0, 10, 20])
ax.set_zlim(0)
ax.set_zticks([0, 10, 20])
ax.legend(loc="upper left")

fig.savefig(str(RESULT_PATH / "irwi_example_boxes.pdf"), bbox_inches="tight")
