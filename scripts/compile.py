#!/usr/bin/env python3

import argparse
import common


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
    parser.add_argument("--debug-stats",
                        dest="debug_stats",
                        action="store_true",
                        default=argparse.SUPPRESS,
                        help="Show debugging stats (currently quickload only).")

    result = parser.parse_args()
    common.compile(**vars(result))
