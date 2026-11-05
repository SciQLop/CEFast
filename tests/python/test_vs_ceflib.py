"""Differential check against IRAP's CEFLIB, the reference CEF reader.

CEFLIB is a global singleton (one file per process) and writes traces to stdout, so each corpus
file is loaded and compared in its own subprocess: this file with --compare <file> --out <json>.
The parent only collects verdicts.

Normalisation (see the design, "Testing"): variable names lose their __<DATASET_ID> suffix on both
sides, CEFLIB's synthetic DELTA_PLUS/MINUS constants are ignored, FLOAT compares at rtol 1e-6 and
DOUBLE at 1e-12 (CEFLIB's hand-rolled parser is not correctly rounded), times compare to 1 ms
(CEFLIB keeps milliseconds since 1958 as doubles), CHAR compares stripped, BYTE is skipped
(CEFLIB's Python binding has no BYTE case).
"""

import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

import numpy as np

import cefast

RESOURCES = pathlib.Path(
    os.environ.get(
        "CEFAST_TEST_RESOURCES",
        pathlib.Path(__file__).resolve().parents[1] / "resources",
    )
)
CORPUS = RESOURCES / "csa"

CEFLIB_DIR = os.environ.get(
    "CEFLIB_PATH", os.path.expanduser("~/.cache/cefast-ceflib/CEFLIB/PYTHON")
)
if CEFLIB_DIR not in sys.path:
    sys.path.insert(0, CEFLIB_DIR)

try:
    import ceflib
except ImportError:  # pragma: no cover - depends on the optional CEFLIB build
    ceflib = None

# CEFLIB 1.8.6 cannot read these at all; each one is a row in docs/accepted-deviations.md
# ("CEFLIB differences"). cefast loads them (corpus test covers them).
CEFLIB_UNREADABLE = {
    "CC_CP_AUX_MAARBLE_KIR_ULF_PC35__20030610_000000_20030610_060000_V141119.cef":
        "CEFLIB Get_token rejects the backslash continuation at line 301",
}

# CEFLIB keeps ISO times as milliseconds since 1958-01-01 (MILLI_TO_1958 in LIB/src/TIMES.c).
MILLI_TO_1958 = 378_691_200_000
TIME_TOLERANCE_MS = 1.0


def short_name(name):
    return name.split("__", 1)[0]


def ms_since_1958(nanoseconds_since_1970):
    return nanoseconds_since_1970.astype("float64") / 1e6 + MILLI_TO_1958


def compare_values(name, var, lib):
    problems = []
    kind = var.value_type
    values = var.values

    if kind == "BYTE":
        return []  # cef_var() has no BYTE case: CEFLIB cannot read it at all

    if kind == "CHAR":
        ours = [str(value).strip() for value in values.reshape(-1)]
        theirs = [str(value).strip() for value in lib.reshape(-1)]
        bad = [
            index
            for index, (left, right) in enumerate(zip(ours, theirs))
            if left != right
        ]
        if bad:
            problems.append(
                f"{name}: {len(bad)} CHAR values differ, first at {bad[0]!r} "
                f"({ours[bad[0]]!r} != {theirs[bad[0]]!r})"
            )
        return problems

    if kind in ("ISO_TIME", "ISO_TIME_RANGE"):
        ours = ms_since_1958(values)
        theirs = lib.astype("float64")
        difference = np.abs(ours - theirs)
        bad = np.flatnonzero(~(difference <= TIME_TOLERANCE_MS))
        if bad.size:
            first = int(bad[0])
            problems.append(
                f"{name}: {bad.size}/{ours.size} times differ by more than 1 ms, first at "
                f"{first}: {difference.flat[first]:.3f} ms"
            )
        return problems

    if kind in ("FLOAT", "DOUBLE"):
        rtol = 1e-6 if kind == "FLOAT" else 1e-12
        bad = np.flatnonzero(
            ~np.isclose(values, lib, rtol=rtol, atol=0.0, equal_nan=True)
        )
        if bad.size:
            first = int(bad[0])
            problems.append(
                f"{name}: {bad.size}/{values.size} {kind} values differ, first at {first}: "
                f"{values.flat[first]!r} != {lib.flat[first]!r}"
            )
        return problems

    bad = np.flatnonzero(values != lib)
    if bad.size:
        first = int(bad[0])
        problems.append(
            f"{name}: {bad.size}/{values.size} INT values differ, first at {first}: "
            f"{values.flat[first]!r} != {lib.flat[first]!r}"
        )
    return problems


def compare_attributes(name, var, problems):
    attributes = ceflib.vattrs(name)
    sizes = attributes.get("SIZES")
    if sizes is not None:
        parts = sizes if isinstance(sizes, (list, tuple)) else [sizes]
        expected = [int(part) for entry in parts for part in str(entry).replace(" ", "").split(",") if part]
        if expected != list(var.sizes):
            problems.append(f"{name}: SIZES {list(var.sizes)} != CEFLIB {expected}")
    fillval = attributes.get("FILLVAL")
    if isinstance(fillval, (list, tuple)):
        fillval = fillval[0] if fillval else None
    ours = var.attributes.get("FILLVAL")
    if fillval is not None and isinstance(ours, (int, float)):
        try:
            if not np.isclose(float(fillval), float(ours), rtol=1e-12, atol=0.0):
                problems.append(f"{name}: FILLVAL {ours!r} != CEFLIB {fillval!r}")
        except ValueError:
            problems.append(
                f"{name}: FILLVAL {ours!r} is not numeric unlike CEFLIB's {fillval!r}"
            )


def compare_file(path):
    problems = []
    if ceflib.read(str(path)) != 0:
        if path.name in CEFLIB_UNREADABLE:
            return []
        return [f"CEFLIB could not read {path.name}"]

    file = cefast.load(path)
    theirs = set(ceflib.varnames())
    ours = {short_name(name): file[name] for name in file}

    missing = sorted(set(ours) - theirs)
    if missing:
        problems.append(f"variables CEFLIB does not expose: {missing}")
    extra = sorted(theirs - set(ours))
    if extra:
        # CEFLIB creates synthetic K001.. variables for DELTA_PLUS/DELTA_MINUS constants.
        unexpected = [name for name in extra if not name.startswith("K")]
        if unexpected:
            problems.append(f"variables CEFLIB has and cefast does not: {unexpected}")

    for name, var in ours.items():
        if name not in theirs:
            continue
        lib = ceflib.var(name)
        if tuple(var.values.shape) != tuple(lib.shape):
            problems.append(
                f"{name}: shape {tuple(var.values.shape)} != CEFLIB {tuple(lib.shape)}"
            )
            continue
        if var.is_nrv == bool(ceflib.record_varying(name)):
            problems.append(
                f"{name}: record_varying disagrees (cefast is_nrv={var.is_nrv})"
            )
        problems.extend(compare_values(name, var, lib))
        compare_attributes(name, var, problems)

    records = ceflib.records()
    for name, var in ours.items():
        if not var.is_nrv and var.values.shape[0] != records:
            problems.append(f"{name}: records {var.values.shape[0]} != CEFLIB {records}")
            break

    globals_ = file.attributes
    gattributes = ceflib.gattrs()
    for key in ("FILE_NAME", "FILE_FORMAT_VERSION"):
        if globals_.get(key) != gattributes.get(key):
            problems.append(
                f"global {key} {globals_.get(key)!r} != CEFLIB {gattributes.get(key)!r}"
            )

    return problems


def main(argv):
    path = pathlib.Path(argv[argv.index("--compare") + 1])
    out = pathlib.Path(argv[argv.index("--out") + 1])
    try:
        problems = compare_file(path)
    except Exception as error:  # noqa: BLE001 - the verdict has to reach the parent
        problems = [f"{type(error).__name__}: {error}"]
    out.write_text(json.dumps({"file": path.name, "problems": problems}))


class TestAgainstCeflib(unittest.TestCase):
    @unittest.skipUnless(ceflib is not None, f"CEFLIB not importable from {CEFLIB_DIR}")
    def test_every_corpus_file_matches(self):
        files = sorted(CORPUS.glob("*.cef"))
        self.assertEqual(len(files), 41)
        self.assertTrue(set(CEFLIB_UNREADABLE) <= {path.name for path in files})
        failures = []
        for path in files:
            with tempfile.TemporaryDirectory() as directory:
                out = pathlib.Path(directory) / "verdict.json"
                process = subprocess.run(
                    [
                        sys.executable,
                        str(pathlib.Path(__file__).resolve()),
                        "--compare",
                        str(path),
                        "--out",
                        str(out),
                    ],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if not out.exists():
                    failures.append(
                        f"{path.name}: no verdict ({process.returncode})\n"
                        f"{process.stderr[-2000:]}"
                    )
                    continue
                verdict = json.loads(out.read_text())
                if verdict["problems"]:
                    failures.append(f"{path.name}: " + " | ".join(verdict["problems"]))
        self.assertEqual(failures, [], "\n".join(failures))


if __name__ == "__main__":
    if "--compare" in sys.argv:
        main(sys.argv)
    else:
        unittest.main()
