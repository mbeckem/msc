#!/usr/bin/env python3

import collections
import json
import os
import resource
import shutil
import subprocess

from pathlib import Path


def _times(iterable, num):
    for i in iterable:
        yield i * num


def walk_generated(entries, labels):
    return DATA_PATH / "random-walk-n{}-l{}.entries".format(entries, labels)


def make_dir(p, check=False):
    path = Path(p)
    if check and not path.exists():
        raise Exception("Path {} does not exist".format(str))
    path.mkdir(parents=True, exist_ok=True)
    return path.resolve()


def reset_dir(p):
    remove(p)
    return make_dir(p)


def remove(path):
    """Recursively delete a file or directory.
    Must be a child (direct or indirect) of tmp or output"""
    if not path.exists():
        return

    path = path.resolve()
    output = OUTPUT_PATH.resolve()
    tmp = TMP_PATH.resolve()
    if not (tmp in path.parents) and \
            not (output in path.parents):
        raise RuntimeError(
            "Not a child of TMP_PATH, or OUTPUT_PATH")

    if path.is_dir():
        shutil.rmtree(str(path))
    else:
        path.unlink()


def file_size(path):
    """Returns the sum of all files in the directory.
    Also works with single files."""

    if not path.exists():
        raise RuntimeError("Path does not exist")

    if path.is_file():
        return path.stat().st_size
    if path.is_dir():
        size = 0
        for child in path.iterdir():
            size += file_size(child)
        return size
    return 0


# Takes a directionary `d` and returns the keys
# specified in `args` as a list.
def values(d, *args):
    return [d[arg] for arg in args]


# Important Folders
BUILD_PATH = make_dir("build")
CODE_PATH = make_dir("code")
DATA_PATH = make_dir("data")
OUTPUT_PATH = make_dir("output")
RESULT_PATH = make_dir("results")
SCRIPTS_PATH = make_dir("scripts")
TMP_PATH = make_dir("tmp")

# Change this when the programs are not in the PATH
CMAKE = "cmake"
MAKE = "make"

# Commands
ALGORITHM_EXAMPLES = BUILD_PATH / "algorithm_examples"
GENERATOR = BUILD_PATH / "generator"
GEOLIFE_GENERATOR = BUILD_PATH / "geolife_generator"
ID_SHUFFLE = BUILD_PATH / "id_shuffle"
OSM_GENERATOR = BUILD_PATH / "osm_generator"
HILBERT_CURVE = BUILD_PATH / "hilbert_curve"
LOADER = BUILD_PATH / "loader"
QUERY = BUILD_PATH / "query"
STATS = BUILD_PATH / "stats"

# Random walk with fixed size and growing number of labels (~evenly
# distributed). More labels == more load in the inverted indices and significantly more
# data in postings lists.
RANDOM_WALK_VARYING_LABELS = [
    (2000000, labels, walk_generated(2000000, labels))
    for labels in _times(range(1, 11), 200)
]

# Geolife dataset has ~5.4 million entries with ~10 labels.
GEOLIFE = (5400000, DATA_PATH / "geolife.entries")

GEOLIFE_SHUFFLED = (5400000, DATA_PATH / "geolife-shuffled.entries")

# Entry file with 10 million entries and 100 labels (~evenly distributed).
RANDOM_WALK = (10000000, DATA_PATH / "random-walk.entries")

RANDOM_WALK_SMALL = (64000, DATA_PATH / "random-walk-small.entries")

# 200 million entries (with only 10 labels).
RANDOM_WALK_LARGE = (100000000, DATA_PATH / "random-walk-large.entries")

# 4 million entries (about 4,5k routes).
# The labels are street names.
OSM_ROUTES = (4000000, DATA_PATH / "osm.entries")

OSM_ROUTES_SHUFFLED = (4000000, DATA_PATH / "osm-shuffled.entries")

# Make sure that we can open enough files (quickload requires many
# buckets). This is equivalent to `ulimit -Sn 64000` in the shell.
limits = resource.getrlimit(resource.RLIMIT_NOFILE)
resource.setrlimit(resource.RLIMIT_NOFILE, (64000, limits[1]))


# Builds a tree using the specified parameters.
# Uses the given algorithm to insert entries from the entry file at "entries_path".
# The tree will be stored at "tree_path". Any existing tree at that location
# will be removed, unless keep_existing is true.
# Offset and Limit control where the tool starts in the entry file and how many
# entries it will insert (offset None means "start at the beginning", limit
# None means "insert everything up to EOF").
def build_tree(algorithm, tree_path, entries_path, logfile, beta=0.5,
               memory=64, offset=None, limit=None, keep_existing=False):
    if not keep_existing:
        # Make sure the tree does not exist yet.
        remove(tree_path)

    stats_path = TMP_PATH / "stats.json"
    remove(stats_path)

    tpie_temp_path = make_dir(TMP_PATH / "tpie")
    args = [
        str(LOADER),
        "--algorithm", algorithm,
        "--entries", str(entries_path),
        "--tree", str(tree_path),
        "--beta", str(beta),
        "--tmp", str(tpie_temp_path),
        "--stats", str(stats_path),
        "--max-memory", str(memory)
    ]
    if offset is not None:
        args.extend(["--offset", str(offset)])
    if limit is not None:
        args.extend(["--limit", str(limit)])

    subprocess.check_call(args, stdout=logfile)
    print("\n\n", file=logfile, flush=True)

    with stats_path.open() as stats_file:
        return json.load(stats_file)


_cpus = os.cpu_count()


def bool_str(cond):
    return "1" if cond else "0"


def compile(type="Release",
            jobs=_cpus,
            block_size=4096,
            beta="normal",
            lambda_=40,
            leaf_fanout=0,
            internal_fanout=0,
            bloom_filters=False,
            naive_node_building=False,
            cheap_quickload=False,
            debug_stats=False):
    defines = {
        "CMAKE_BUILD_TYPE": type,
        "USE_BLOOM": bool_str(bloom_filters),
        "USE_NAIVE_NODE_BUILDING": bool_str(naive_node_building),
        "BLOCK_SIZE": str(block_size),
        "BETA": beta,
        "LAMBDA": str(lambda_),
        "LEAF_FANOUT": str(leaf_fanout),
        "INTERNAL_FANOUT": str(internal_fanout),
        "CHEAP_QUICKLOAD": bool_str(cheap_quickload),
        "BUILD_INSPECTOR": "1",
        "BUILD_OSM": "1",
        "DEBUG_STATS": bool_str(debug_stats)
    }

    args = [str(CMAKE)]
    for key, value in defines.items():
        args.extend(["-D", "{}={}".format(key, value)])
    args.append(str(CODE_PATH))

    cwd = str(BUILD_PATH)
    subprocess.check_call(args, cwd=cwd)
    subprocess.check_call([str(MAKE), "-j{}".format(jobs)], cwd=cwd)


if __name__ == "__main__":
    print(
        "Paths:\n"
        "  Build path: {build}\n"
        "  Data path: {data}\n"
        "  Output path: {output}\n"
        "  Result path: {results}\n"
        "  Tmp path: {tmp}".format(
            build=BUILD_PATH.resolve(),
            data=DATA_PATH.resolve(),
            output=OUTPUT_PATH.resolve(),
            results=RESULT_PATH.resolve(),
            tmp=TMP_PATH.resolve()
        )
    )
