#!/usr/bin/env python3

import commands
import common

from common import OUTPUT_PATH
from compile import compile
from datasets import GEOLIFE, OSM_ROUTES, RANDOM_WALK

# Build tree for the beta modes "normal", "increasing", "decreasing".
# "normal" == same beta value (0.5) for all tree levels.
# "increasing" == beta increases with nodes that have a higher level,
#                 i.e. spatial cost becomes more important with increasing
#                 distance to the leaves.
# "decreasing" == the other way around.
if __name__ == "__main__":
    tree_dir = common.reset_dir(OUTPUT_PATH / "beta_trees")

    def tree_path(dataset, algorithm, mode):
        return tree_dir / "{}-{}-beta_{}".format(dataset, algorithm, mode)

    with (OUTPUT_PATH / "build_beta_trees.log").open("w") as logfile:
        datasets = [("geolife", *GEOLIFE), ("osm", *OSM_ROUTES)]
        for mode in ["increasing", "decreasing"]:
            compile(beta=mode)

            for dataset, entries, data_path in datasets:
                commands.build_tree("obo", tree_path=tree_path(dataset, "obo", mode),
                                    entries_path=data_path, logfile=logfile,
                                    limit=entries)
                commands.build_tree("quickload", tree_path=tree_path(dataset, "quickload", mode),
                                    entries_path=data_path, logfile=logfile,
                                    limit=entries)
