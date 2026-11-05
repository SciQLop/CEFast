"""Smoke test for a built wheel: run by cibuildwheel on every platform and interpreter.

Deliberately self-contained: a small CEF file is written inline, so it needs neither the CSA
corpus nor any test fixture. Run with `python tests/wheel/test_wheel.py`.
"""

import gzip
import pathlib
import tempfile

import numpy as np

import cefast

SAMPLE = b"""FILE_NAME = "smoke.cef"
FILE_FORMAT_VERSION = "CEF-2.0"
START_META = MISSION
  VALUE_TYPE = CHAR
  ENTRY = Cluster
END_META = MISSION
START_VARIABLE = time_tags
  VALUE_TYPE = ISO_TIME
  FILLVAL = "9999-99-99T99:99:99Z"
END_VARIABLE = time_tags
START_VARIABLE = B_vec_xyz_gse
  VALUE_TYPE = FLOAT
  SIZES = 3
  FILLVAL = -1e31
END_VARIABLE = B_vec_xyz_gse
START_VARIABLE = note
  VALUE_TYPE = CHAR
END_VARIABLE = note
DATA_UNTIL = END_DATA
2001-02-01T00:00:00.000Z, 1, 2, 3, "spin"
2001-02-01T00:00:04.000Z, 4, 5, 6, "a, b"
END_DATA
"""


def check(file):
    assert file.attributes["MISSION"] == "Cluster"
    times = file["time_tags"].values
    assert times.dtype == np.dtype("datetime64[ns]")
    assert times[-1] == np.datetime64("2001-02-01T00:00:04")
    field = file["B_vec_xyz_gse"]
    assert field.sizes == (3,)
    assert field.values.shape == (2, 3)
    assert field.values[1, 2] == 6.0
    assert file["note"].values[-1] == "a, b"


def main():
    file = cefast.load(SAMPLE)
    check(file)
    check(cefast.load(gzip.compress(SAMPLE)))
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory) / "smoke.cef"
        path.write_bytes(SAMPLE)
        check(cefast.load(path))
        check(cefast.load(str(path)))
    try:
        cefast.load(
            b'FILE_FORMAT_VERSION = "CEF-2.0"\nDATA_UNTIL = END_DATA\n1,2\nEND_DATA\n'
        )
    except cefast.ParseError:
        pass
    else:
        raise AssertionError("a malformed file must raise cefast.ParseError")
    print(
        f"cefast {cefast.__version__} ok: {len(file)} variables, {len(file['note'].values)} records"
    )


if __name__ == "__main__":
    main()
