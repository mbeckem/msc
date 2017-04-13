#!/usr/bin/env python3

import os
import subprocess

import common

_cpus = os.cpu_count()


def bool_str(cond):
    return "1" if cond else "0"


def compile(type="Release",
            jobs=_cpus,
            block_size=4096,
            lambda_=40,
            leaf_fanout=0,
            internal_fanout=0,
            bloom_filters=False,
            naive_node_building=False):
    defines = {
        "CMAKE_BUILD_TYPE": type,
        "USE_BLOOM": bool_str(bloom_filters),
        "USE_NAIVE_NODE_BUILDING": bool_str(naive_node_building),
        "BLOCK_SIZE": str(block_size),
        "LAMBDA": str(lambda_),
        "LEAF_FANOUT": str(leaf_fanout),
        "INTERNAL_FANOUT": str(internal_fanout),
    }

    args = [common.CMAKE]
    for key, value in defines.items():
        args.extend(["-D", "{}={}".format(key, value)])
    args.append(str(common.CODE_PATH))

    cwd = str(common.BUILD_PATH)
    subprocess.check_call(args, cwd=cwd)
    subprocess.check_call([common.MAKE, "-j{}".format(jobs)], cwd=cwd)

if __name__ == "__main__":
    compile()
