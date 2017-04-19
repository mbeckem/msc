import collections
import json
import subprocess
import resource

import common
from common import LOADER, TMP_PATH

BuildStats = collections.namedtuple("BuildStats", [
    "read_io", "write_io", "total_io", "duration", "leaf_fanout", "internal_fanout"
])

# Make sure that we can open enough files (quickload requires many
# buckets). This is equivalent to `ulimit -Sn 16384` in the shell.
limits = resource.getrlimit(resource.RLIMIT_NOFILE)
resource.setrlimit(resource.RLIMIT_NOFILE, (2 ** 14, limits[1]))


def build_tree(algorithm, tree_path, entries_path, logfile, memory=64, limit=None, keep_existing=False):
    if not keep_existing:
        # Make sure the tree does not exist yet.
        common.remove(tree_path)

    stats_path = TMP_PATH / "stats.json"
    common.remove(stats_path)

    tpie_temp_path = common.make_dir(TMP_PATH / "tpie")
    args = [
        str(LOADER),
        "--algorithm", algorithm,
        "--entries", str(entries_path),
        "--tree", str(tree_path),
        "--tmp", str(tpie_temp_path),
        "--stats", str(stats_path),
        "--max-memory", str(memory)
    ]
    if limit is not None:
        args.extend(["--limit", str(limit)])

    subprocess.check_call(args, stdout=logfile)
    print("\n\n", file=logfile, flush=True)

    stats = None
    with stats_path.open() as stats_file:
        stats = json.load(stats_file)

    filtered_stats = {key: stats[key] for key in BuildStats._fields}
    return BuildStats(**filtered_stats)
