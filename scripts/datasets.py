#!/usr/bin/env python3

import subprocess

from common import GENERATOR, DATA_PATH


def _times(iterable, num):
    for i in iterable:
        yield i * num


def _check(path):
    if not path.exists():
        raise RuntimeError("Path does not exist: {}".format(path))
    return path


def osm_generated(entries):
    return _check(DATA_PATH / "osm-generated-n{}.entries".format(entries))


def walk_generated(entries, labels):
    return _check(DATA_PATH / "walk-generated-n{}-l{}.entries".format(entries, labels))

# Random walk with fixed size and growing number of labels (~evenly
# distributed). More labels == more load in the inverted indices and significantly more
# data in postings lists.
RANDOM_WALK_VARYING_LABELS = [
    (5000000, labels, walk_generated(5000000, labels))
    for labels in [10, 20, 30, 40, 50, 100,
                   150, 200, 250, 500, 1000, 2000, 5000, 10000]
]

# Geolife dataset has ~5.4 million entries with ~10 labels.
GEOLIFE = (5400000, _check(DATA_PATH / "geolife.entries"))

# Entry file with 10 million entries and 100 labels (~evenly distributed).
RANDOM_WALK = (10000000, walk_generated(10000000, 100))

RANDOM_WALK_SMALL = (64000, _check(DATA_PATH / "walk-generated-small.entries"))

# 100 million entries (with only 10 labels).
RANDOM_WALK_LARGE = (400000000, _check(
    DATA_PATH / "random-walk-large.entries"))

# 5 million entries (about 4,5k routes).
# The labels are street names.
OSM_ROUTES = (4000000, osm_generated(4000000))
