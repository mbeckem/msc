#!/usr/bin/env python3

import common

from common import OUTPUT_PATH, GEOLIFE, OSM_ROUTES, GEOLIFE_SHUFFLED, OSM_ROUTES_SHUFFLED
from common import compile

# Build trees for different values of beta.
if __name__ == "__main__":
    tree_dir = OUTPUT_PATH / "variants"
    datasets = [("geolife", *GEOLIFE), ("osm", *OSM_ROUTES)]
    algorithms = ["obo", "quickload"]

    current_compile_flags = None

    def build_if_missing(algorithm, tree_path, entries_path, logfile, beta=0.5, limit=None, compile_flags={}):
        if tree_path.exists():
            print("{} exists, skipping ...".format(tree_path))
            return

        print("Building {}".format(tree_path))

        # Rebuild if the flags differ.
        global current_compile_flags
        if current_compile_flags is None or current_compile_flags != compile_flags:
            compile(**compile_flags)
            current_compile_flags = compile_flags

        common.build_tree(algorithm,
                          tree_path=tree_path, entries_path=entries_path,
                          logfile=logfile, beta=beta, limit=limit)

    with (OUTPUT_PATH / "tree_variants.log").open("w") as logfile:
        def build_shuffled():
            def tree_path(dataset):
                return tree_dir / "{}-{}".format(dataset, "quickload")

            for dataset, entries, data_path in [
                    ("geolife-shuffled", *GEOLIFE_SHUFFLED),
                    ("osm-shuffled", *OSM_ROUTES_SHUFFLED)]:
                build_if_missing("quickload", tree_path=tree_path(dataset),
                                 entries_path=data_path, logfile=logfile, limit=entries)

        def build_str_variants():
            def tree_path(dataset, algorithm):
                return tree_dir / "{}-{}".format(dataset, algorithm)

            for dataset, entries, data_path in datasets:
                for algorithm in ["str-plain", "str-ll"]:
                    build_if_missing(algorithm, tree_path=tree_path(dataset, algorithm),
                                     entries_path=data_path, logfile=logfile, limit=entries)

        def build_beta_values():
            def tree_path(dataset, algorithm, beta):
                return tree_dir / "{}-{}-beta-{}".format(dataset, algorithm, beta)

            # 0.5 is already the default (built by eval_tree_building).
            for beta in [0.25, 0.75, 1.0]:
                for dataset, entries, data_path in datasets:
                    for algorithm in algorithms:
                        build_if_missing(algorithm,
                                         tree_path=tree_path(
                                             dataset, algorithm, beta),
                                         entries_path=data_path, beta=beta,
                                         limit=entries, logfile=logfile)

        def build_beta_modes():
            def tree_path(dataset, algorithm, mode):
                return tree_dir / "{}-{}-beta-{}".format(dataset, algorithm, mode)

            for mode in ["increasing", "decreasing"]:
                for dataset, entries, data_path in datasets:
                    for algorithm in algorithms:
                        build_if_missing(algorithm, tree_path=tree_path(dataset, algorithm, mode),
                                         entries_path=data_path, logfile=logfile, limit=entries,
                                         compile_flags=dict(beta=mode))

        def build_bloom_filters():
            def tree_path(dataset, algorithm):
                return tree_dir / "{}-{}-bloom".format(dataset, algorithm)

            for dataset, entries, data_path in datasets:
                for algorithm in algorithms:
                    build_if_missing(algorithm, tree_path=tree_path(dataset, algorithm),
                                     entries_path=data_path, logfile=logfile, limit=entries,
                                     compile_flags=dict(bloom_filters=True))

        def build_fanout_trees():
            # Only quickload, different fanouts. Dataset: geolife.
            algorithm = "quickload"
            dataset, entries, data_path = ("geolife", *GEOLIFE)

            def tree_path(fanout):
                return tree_dir / "{}-{}-fanout-{}".format(dataset, algorithm, fanout)

            for fanout in [32, 50, 64]:
                build_if_missing(algorithm, tree_path=tree_path(fanout),
                                 entries_path=data_path, logfile=logfile, limit=entries,
                                 compile_flags=dict(leaf_fanout=fanout, internal_fanout=fanout))

        # These require special compile options.
        build_bloom_filters()
        build_beta_modes()
        build_fanout_trees()

        # These restore the normal compile options.
        build_shuffled()
        build_beta_values()
        build_str_variants()
