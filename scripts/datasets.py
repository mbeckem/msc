#!/usr/bin/env python3

import subprocess

from common import DATA_PATH


def _times(iterable, num):
    for i in iterable:
        yield i * num


def walk_generated(entries, labels):
    return DATA_PATH / "random-walk-n{}-l{}.entries".format(entries, labels)

# Random walk with fixed size and growing number of labels (~evenly
# distributed). More labels == more load in the inverted indices and significantly more
# data in postings lists.
RANDOM_WALK_VARYING_LABELS = [
    (2000000, labels, walk_generated(2000000, labels))
    for labels in _times(range(1, 11), 200)
]

# Geolife dataset has ~5.4 million entries with ~10 labels.
GEOLIFE = (5400000, DATA_PATH / "geolife.entries")

# Entry file with 10 million entries and 100 labels (~evenly distributed).
RANDOM_WALK = (10000000, DATA_PATH / "random-walk.entries")

RANDOM_WALK_SMALL = (64000, DATA_PATH / "random-walk-small.entries")

# 100 million entries (with only 10 labels).
RANDOM_WALK_LARGE = (100000000, DATA_PATH / "random-walk-large.entries")

# 4 million entries (about 4,5k routes).
# The labels are street names.
OSM_ROUTES = (4000000, DATA_PATH / "osm.entries")
