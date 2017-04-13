#!/usr/bin/env python3

import os
import shutil
from pathlib import Path


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
    if not (tmp in path.parents) and not (output in path.parents):
        raise RuntimeError("Not a child of TMP_PATH or OUTPUT_PATH")

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

BUILD_PATH = make_dir("build")
CODE_PATH = make_dir("code")
DATA_PATH = make_dir("data")
OUTPUT_PATH = make_dir("output")
RESULT_PATH = make_dir("results")
TMP_PATH = make_dir("tmp")

CMAKE = "cmake"
MAKE = "make"

ALGORITHM_EXAMPLES = BUILD_PATH / "algorithm_examples"
GENERATOR = BUILD_PATH / "generator"
HILBERT_CURVE = BUILD_PATH / "hilbert_curve"
LOADER = BUILD_PATH / "loader"


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
