#!/usr/bin/env python3

import argparse
import os
import subprocess

import common

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
            build_inspector=False,
            build_osm=False,
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
        "BUILD_INSPECTOR": bool_str(build_inspector),
        "BUILD_OSM": bool_str(build_osm),
        "DEBUG_STATS": bool_str(debug_stats)
    }

    args = [common.CMAKE]
    for key, value in defines.items():
        args.extend(["-D", "{}={}".format(key, value)])
    args.append(str(common.CODE_PATH))

    cwd = str(common.BUILD_PATH)
    subprocess.check_call(args, cwd=cwd)
    subprocess.check_call([common.MAKE, "-j{}".format(jobs)], cwd=cwd)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Compile the project.\nAll flags are optional, sensible defaults will be chosen."
    )

    parser.add_argument("--type",
                        type=str,
                        default=argparse.SUPPRESS,
                        help="The build type(e.g. Debug or Release).")
    parser.add_argument("--block-size", metavar="SIZE",
                        dest="block_size", type=int,
                        default=argparse.SUPPRESS,
                        help="The physical block size.")
    parser.add_argument("--beta",
                        type=str,
                        choices=["normal", "increasing", "decreasing"],
                        default=argparse.SUPPRESS,
                        help="The strategy for beta.")
    parser.add_argument("--lambda", metavar="LAMBDA",
                        dest="lambda_", type=int,
                        default=argparse.SUPPRESS,
                        help="The size of a single posting.")
    parser.add_argument("--leaf-fanout", metavar="N",
                        dest="leaf_fanout", type=int,
                        default=argparse.SUPPRESS,
                        help="The number of entries in a leaf node.")
    parser.add_argument("--internal-fanout", metavar="N",
                        dest="internal_fanout", type=int,
                        default=argparse.SUPPRESS,
                        help="The number of entries in a internal node.")
    parser.add_argument("--bloom-filters",
                        dest="bloom_filters",
                        action="store_true",
                        default=argparse.SUPPRESS,
                        help="Enable the use of bloom filters")
    parser.add_argument("--naive-node-building",
                        dest="naive_node_building",
                        action="store_true",
                        default=argparse.SUPPRESS,
                        help="Enable naive node building.")
    parser.add_argument("--cheap-quickload",
                        dest="cheap_quickload",
                        action="store_true",
                        default=argparse.SUPPRESS,
                        help="Use the cheap quickload variant.")
    parser.add_argument("--build-inspector",
                        dest="build_inspector",
                        action="store_true",
                        default=argparse.SUPPRESS,
                        help="Build the inspector tool.")
    parser.add_argument("--build-osm",
                        dest="build_osm",
                        action="store_true",
                        default=argparse.SUPPRESS,
                        help="Build the osm utilities.")
    parser.add_argument("--debug-stats",
                        dest="debug_stats",
                        action="store_true",
                        default=argparse.SUPPRESS,
                        help="Show debugging stats (currently quickload only).")

    result = parser.parse_args()
    compile(**vars(result))
