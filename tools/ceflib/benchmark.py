#!/usr/bin/env python3
"""Median warm load time of every corpus file, cefast vs CEFLIB (5 runs each, after one warm-up).

Both timings are full parses: cefast.load() builds the whole in-memory file, CEFLIB's read()
physically reads every record (then close() releases it, CEFLIB being a global singleton).
Run with the venv on PATH and PYTHONPATH=<cefast build dir>:

    python3 tools/ceflib/benchmark.py [--runs 5] [--gzip]

--gzip compresses each corpus file in memory (as CSA serves .cef.gz) and times
cefast.load(bytes); CEFLIB has no in-memory entry point, so its column is n/a.
"""

import argparse
import gzip
import os
import pathlib
import statistics
import sys
import time

RESOURCES = pathlib.Path(
    os.environ.get(
        "CEFAST_TEST_RESOURCES",
        pathlib.Path(__file__).resolve().parents[2] / "tests" / "resources",
    )
)
CORPUS = RESOURCES / "csa"
CEFLIB_DIR = os.environ.get(
    "CEFLIB_PATH", os.path.expanduser("~/.cache/cefast-ceflib/CEFLIB/PYTHON")
)
sys.path.insert(0, CEFLIB_DIR)

import cefast  # noqa: E402
import ceflib  # noqa: E402


def median_time(call, runs):
    call()
    timings = []
    for _ in range(runs):
        start = time.perf_counter()
        call()
        timings.append(time.perf_counter() - start)
    return statistics.median(timings)


def load_ceflib(path):
    if ceflib.read(str(path)) != 0:
        raise RuntimeError(f"CEFLIB cannot read {path.name}")
    ceflib.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument(
        "--gzip",
        action="store_true",
        help="gzip each file in memory and time load(bytes); CEFLIB column is n/a",
    )
    arguments = parser.parse_args()

    files = sorted(CORPUS.glob("*.cef"))
    print(f"{'file':60} {'MB':>6} {'cefast ms':>10} {'CEFLIB ms':>10} {'speedup':>8}")
    ours_all, theirs_all, ratios = [], [], []
    for path in files:
        megabytes = path.stat().st_size / 1048576
        if arguments.gzip:
            payload = gzip.compress(path.read_bytes())
            ours = median_time(lambda: cefast.load(payload), arguments.runs) * 1000
            ours_all.append(ours)
            print(f"{path.name:60} {megabytes:6.2f} {ours:10.2f} {'n/a':>10}")
            continue
        ours = median_time(lambda: cefast.load(path), arguments.runs) * 1000
        try:
            theirs = median_time(lambda: load_ceflib(path), arguments.runs) * 1000
        except RuntimeError:
            print(f"{path.name:60} {megabytes:6.2f} {ours:10.2f} {'unreadable':>10}")
            continue
        ours_all.append(ours)
        theirs_all.append(theirs)
        ratios.append(theirs / ours if ours else float("inf"))
        print(
            f"{path.name:60} {megabytes:6.2f} {ours:10.2f} {theirs:10.2f} {theirs / ours:8.2f}x"
        )

    print()
    print(
        f"total load time: cefast {sum(ours_all):.1f} ms, CEFLIB {sum(theirs_all):.1f} ms"
    )
    if arguments.gzip:
        print(f"total gzipped load time: cefast {sum(ours_all):.1f} ms, CEFLIB n/a")
        return
    print(
        f"per-file speedup: median {statistics.median(ratios):.2f}x, "
        f"min {min(ratios):.2f}x, max {max(ratios):.2f}x"
    )
    print(f"total speedup: {sum(theirs_all) / sum(ours_all):.2f}x")


if __name__ == "__main__":
    main()
