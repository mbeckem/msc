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


def _osm_generated(entries):
    return _check(DATA_PATH / "osm-generated-n{}.entries".format(entries))


def _walk_generated(entries, labels):
    return _check(DATA_PATH / "walk-generated-n{}-l{}.entries".format(entries, labels))

RANDOM_WALK_VARYING_LABELS = [
    (5000000, labels, _walk_generated(5000000, labels))
    for labels in [10, 20, 30, 40, 50, 100,
                   150, 200, 250, 500, 1000, 2000, 5000, 10000]
]

RANDOM_WALK_VARYING_SIZE = [
    (entries, 100, _walk_generated(entries, 100))
    for entries in _times(range(1, 7), 1000000)
]

OSM_VARYING_SIZE = [
    (entries, None, _osm_generated(entries))
    for entries in _times(range(1, 9), 500000)
]
