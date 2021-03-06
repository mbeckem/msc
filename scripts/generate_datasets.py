#!/usr/bin/env python3

import subprocess

from common import GENERATOR, GEOLIFE_GENERATOR, OSM_GENERATOR, ID_SHUFFLE, DATA_PATH, OUTPUT_PATH
from common import RANDOM_WALK_VARYING_LABELS, OSM_ROUTES, GEOLIFE, RANDOM_WALK, RANDOM_WALK_SMALL, RANDOM_WALK_LARGE
from common import GEOLIFE_SHUFFLED, OSM_ROUTES_SHUFFLED
from common import compile

RANDOM_SEED = 1624827567


def generate_random_walk(entries, labels, path, log, units_per_trajectory=None, maxx=None, maxy=None):
    if path.exists():
        return

    print("Generating {}".format(path))
    args = [
        str(GENERATOR),
        "--output", str(path),
        "--seed", str(RANDOM_SEED),
        "-n", str(entries),
        "-l", str(labels),
    ]
    if units_per_trajectory is not None:
        args.extend(["-m", str(units_per_trajectory)])
    if maxx is not None:
        args.extend(["-x", str(maxx)])
    if maxy is not None:
        args.extend(["-y", str(maxy)])

    subprocess.check_call(args, stdout=log)

if __name__ == "__main__":
    with (OUTPUT_PATH / "generate_datasets.log").open("w") as logfile:
        for entries, labels, path in RANDOM_WALK_VARYING_LABELS:
            generate_random_walk(entries, labels, path, log=logfile)

        entries, path = RANDOM_WALK_SMALL
        generate_random_walk(entries, 10, path, log=logfile)

        entries, path = RANDOM_WALK
        generate_random_walk(entries, 100, path, log=logfile)

        entries, path = RANDOM_WALK_LARGE
        generate_random_walk(entries, 10, path,
                             maxx=200000, maxy=200000,
                             units_per_trajectory=20000, log=logfile)

        entries, path = OSM_ROUTES
        if not path.exists():
            print("Generating {}".format(path))
            with path.with_suffix(".trajectories").open("w") as out:
                subprocess.check_call([
                    str(OSM_GENERATOR),
                    "--output", str(path),
                    "--strings", str(path.with_suffix(".strings")),
                    "--map", str(DATA_PATH / "osm" / "germany-latest.osrm"),
                    "-n", str(entries)
                ], stdout=out)

        entries, path = OSM_ROUTES_SHUFFLED
        if not path.exists():
            print("Generating {}".format(path))
            subprocess.check_call([
                str(ID_SHUFFLE),
                "--input", str(OSM_ROUTES[1]),
                "--output", str(path),
            ])

        entries, path = GEOLIFE
        if not path.exists():
            print("Generating {}".format(path))
            with path.with_suffix(".trajectories").open("w") as out:
                subprocess.check_call([
                    str(GEOLIFE_GENERATOR),
                    "--output", str(path),
                    "--strings", str(path.with_suffix(".strings")),
                    "--log", str(path.with_suffix(".trajectories")),
                    "--data", str(DATA_PATH /
                                  "Geolife Trajectories 1.3" / "Data")
                ])

        entries, path = GEOLIFE_SHUFFLED
        if not path.exists():
            print("Generating {}".format(path))
            subprocess.check_call([
                str(ID_SHUFFLE),
                "--input", str(GEOLIFE[1]),
                "--output", str(path),
            ])
