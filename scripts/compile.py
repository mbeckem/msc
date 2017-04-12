#!/usr/bin/env python3

import os
import subprocess

import commands

_cpus = os.cpu_count()


def bool_str(cond):
    return "1" if cond else "0"


def compile(type="Release",
            jobs=_cpus,
            bloom_filters=False,
            naive_node_building=False):
    defines = {
        "CMAKE_BUILD_TYPE": type,
        "USE_BLOOM": bool_str(bloom_filters),
        "USE_NAIVE_NODE_BUILDING": bool_str(naive_node_building)
    }

    args = [commands.CMAKE]
    for key, value in defines.items():
        args.extend(["-D", "{}={}".format(key, value)])
    args.append(str(commands.CODE_PATH))

    cwd = str(commands.BUILD_PATH)
    subprocess.check_call(args, cwd=cwd)
    subprocess.check_call([commands.MAKE, "-j{}".format(jobs)], cwd=cwd)

if __name__ == "__main__":
    compile()
