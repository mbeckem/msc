#!/usr/bin/env python3

import os
from pathlib import Path


def _make_path(str, check=False):
    path = Path(str)
    if check and not path.exists():
        raise Exception("Path {} does not exist".format(str))
    path.mkdir(parents=True, exist_ok=True)
    return path

BUILD_PATH = _make_path("build")
DATA_PATH = _make_path("data")
OUTPUT_PATH = _make_path("output")
RESULT_PATH = _make_path("results")
TMP_PATH = _make_path("tmp")

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
