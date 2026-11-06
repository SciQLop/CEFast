import gc
import gzip
import os
import pathlib
import re
import signal
import sys
import time
import unittest

import numpy as np

import cefast

RESOURCES = pathlib.Path(os.environ["CEFAST_TEST_RESOURCES"])
CORPUS = RESOURCES / "csa"
FIXTURES = RESOURCES / "fixtures"


def corpus_file(dataset):
    """The CSA files are not in the repository: fetch them with tools/fetch_csa_corpus.py and
    pick them by dataset prefix, since CSA changes the _Vxxxxxx version suffix over time."""
    files = sorted(CORPUS.glob(f"{dataset}__*.cef"))
    if not files:
        raise unittest.SkipTest(
            f"{dataset} is not in {CORPUS}: run `python -I tools/fetch_csa_corpus.py` from the "
            "repository root to download the CSA sample corpus"
        )
    return files[0]


FILL_TIME_FILE = b"""FILE_NAME = "fill.cef"
FILE_FORMAT_VERSION = "CEF-2.0"
START_VARIABLE = t
  VALUE_TYPE = ISO_TIME
  FILLVAL = "9999-99-99T99:99:99Z"
END_VARIABLE = t
DATA_UNTIL = END_DATA
9999-99-99T99:99:99Z
2000-01-01T00:00:00Z
END_DATA
"""


class TestLoad(unittest.TestCase):
    def test_load_path_and_bytes_equal(self):
        from_path = cefast.load(corpus_file("C1_CP_WHI_ELECTRON_DENSITY"))
        from_bytes = cefast.load(corpus_file("C1_CP_WHI_ELECTRON_DENSITY").read_bytes())
        self.assertEqual(list(from_path), list(from_bytes))
        for name in from_path:
            np.testing.assert_array_equal(
                from_path[name].values, from_bytes[name].values
            )
        self.assertEqual(from_path.attributes, from_bytes.attributes)

    def test_load_gz_bytes(self):
        loaded = cefast.load(gzip.compress(corpus_file("C1_CP_FGM_SPIN").read_bytes()))
        plain = cefast.load(corpus_file("C1_CP_FGM_SPIN"))
        self.assertEqual(list(loaded), list(plain))
        self.assertEqual(loaded["B_vec_xyz_gse__C1_CP_FGM_SPIN"].values.shape, (286, 3))

    def test_threads_kwarg(self):
        serial = cefast.load(corpus_file("C1_CP_CIS-CODIF_HS_H1_CS"), threads=1)
        parallel = cefast.load(corpus_file("C1_CP_CIS-CODIF_HS_H1_CS"), threads=4)
        self.assertEqual(list(serial), list(parallel))
        for name in serial:
            np.testing.assert_array_equal(serial[name].values, parallel[name].values)

    def test_parse_error_type(self):
        self.assertTrue(issubclass(cefast.ParseError, ValueError))
        with self.assertRaises(cefast.ParseError):
            cefast.load(b'FILE_NAME = "x"\n')

    def test_include_resolver(self):
        files = {
            "common.ceh": (FIXTURES / "include" / "common.ceh").read_bytes(),
            "nested.ceh": (FIXTURES / "include" / "nested.ceh").read_bytes(),
        }
        loaded = cefast.load(
            (FIXTURES / "include" / "main.cef").read_bytes(), include_resolver=files.get
        )
        self.assertEqual(loaded.attributes["FILE_NAME"], "main.cef")
        self.assertEqual(loaded.attributes["MISSION"], "Cluster")
        # Two blocks with the same name come from the two .ceh files: the dict keeps the last.
        self.assertEqual(loaded.attributes["MISSION_REGION"], "Cusp")

    def test_bytearray_locked_during_parse(self):
        # The parse borrows the caller's buffer, so it must stay exported for the whole load.
        original = (FIXTURES / "include" / "main.cef").read_bytes()
        buffer = bytearray(original)
        files = {
            "common.ceh": (FIXTURES / "include" / "common.ceh").read_bytes(),
            "nested.ceh": (FIXTURES / "include" / "nested.ceh").read_bytes(),
        }

        def resolver(name):
            with self.assertRaises(BufferError):
                buffer.extend(b"x")
            return files[name]

        loaded = cefast.load(buffer, include_resolver=resolver)
        self.assertEqual(loaded.attributes["MISSION"], "Cluster")
        self.assertEqual(bytes(buffer), original)

    def test_missing_key(self):
        loaded = cefast.load(corpus_file("C1_CP_FGM_SPIN"))
        self.assertNotIn("nope", loaded)
        with self.assertRaises(KeyError):
            loaded["nope"]


class TestVariables(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.whi = cefast.load(corpus_file("C1_CP_WHI_ELECTRON_DENSITY"))

    def test_time_dtype(self):
        values = self.whi["time_tags__C1_CP_WHI_ELECTRON_DENSITY"].values
        self.assertEqual(values.dtype, np.dtype("datetime64[ns]"))
        self.assertEqual(values.shape, (24410,))
        self.assertEqual(values[-1], np.datetime64("2002-02-02T11:57:13.320"))

    def test_multidim_shape(self):
        values = cefast.load(corpus_file("C1_CP_CIS-CODIF_HS_H1_CS"))[
            "3d_ions__C1_CP_CIS-CODIF_HS_H1_CS"
        ].values
        self.assertEqual(values.shape, (149, 8, 16, 31))
        self.assertEqual(values[0, 0, 0, 0], 0.0)
        self.assertEqual(values[0, 0, 0, 1], 0.0)

    def test_nrv_shape(self):
        theta = cefast.load(corpus_file("C1_CP_CIS-CODIF_HS_H1_CS"))[
            "theta__C1_CP_CIS-CODIF_HS_H1_CS"
        ]
        self.assertTrue(theta.is_nrv)
        self.assertEqual(theta.sizes, (8,))
        self.assertEqual(theta.values.shape, (8,))
        self.assertEqual(theta.values[0], -78.75)

    def test_nrv_scalar_shape(self):
        sta = corpus_file("C1_PP_STA")
        half_interval = cefast.load(sta)["half_interval__C1_PP_STA"]
        self.assertTrue(half_interval.is_nrv)
        self.assertEqual(half_interval.sizes, (1,))
        self.assertEqual(half_interval.values.shape, (1,))
        self.assertEqual(half_interval.values[0], 2.0)

    def test_nrv_scalar_without_sizes_shape(self):
        # No SIZES at all: one element, like CEFLIB's (1,) and like SIZES = 1.
        d1 = corpus_file("D1_SP_AUX")
        half_interval = cefast.load(d1)["Half_interval__D1_SP_AUX"]
        self.assertTrue(half_interval.is_nrv)
        self.assertEqual(half_interval.sizes, ())
        self.assertEqual(half_interval.values.shape, (1,))

    def test_varying_scalar_shape(self):
        time = cefast.load(corpus_file("C1_CP_FGM_SPIN"))["time_tags__C1_CP_FGM_SPIN"]
        self.assertFalse(time.is_nrv)
        self.assertEqual(time.sizes, (1,))
        self.assertEqual(time.values.shape, (286,))

    def test_char_values(self):
        spectrum = self.whi["Spectrum_Type__C1_CP_WHI_ELECTRON_DENSITY"].values
        self.assertEqual(spectrum.dtype.kind, "U")
        self.assertEqual(spectrum.shape, (24410,))
        self.assertEqual(spectrum[-1], "A")

    def test_range_shape(self):
        values = cefast.load(corpus_file("C3_CT_AUX_GRMB"))[
            "time_tags__C3_CT_AUX_GRMB"
        ].values
        self.assertEqual(values.shape, (383, 2))
        self.assertEqual(values.dtype, np.dtype("datetime64[ns]"))
        self.assertEqual(values[0, 0], np.datetime64("2004-12-31T21:04:12"))
        self.assertEqual(values[0, 1], np.datetime64("2005-01-01T03:00:00"))

    def test_dtypes(self):
        whi = self.whi
        self.assertEqual(
            whi["Delta_Time__C1_CP_WHI_ELECTRON_DENSITY"].values.dtype, np.float32
        )
        self.assertEqual(
            whi["Quality__C1_CP_WHI_ELECTRON_DENSITY"].values.dtype, np.int32
        )
        self.assertEqual(
            whi["time_tags__C1_CP_WHI_ELECTRON_DENSITY"].values.dtype,
            np.dtype("datetime64[ns]"),
        )
        edi = cefast.load(corpus_file("C1_CP_EDI_MP"))["Half_interval__C1_CP_EDI_MP"]
        self.assertEqual(edi.value_type, "DOUBLE")
        self.assertEqual(edi.values.dtype, np.float64)
        efw = cefast.load(corpus_file("C1_CP_EFW_L3_E"))
        self.assertEqual(efw["E_bitmask__C1_CP_EFW_L3_E"].values.dtype, np.int32)

    def test_fill_time_is_nat(self):
        values = cefast.load(FILL_TIME_FILE)["t"].values
        self.assertEqual(values.shape, (2,))
        self.assertTrue(np.isnat(values[0]))
        self.assertEqual(values[1], np.datetime64("2000-01-01T00:00:00"))

    def test_attributes_plain_values(self):
        self.assertEqual(self.whi.attributes["MISSION"], "Cluster")
        regions = self.whi.attributes["MISSION_REGION"]
        self.assertIsInstance(regions, list)
        self.assertEqual(len(regions), 11)
        self.assertEqual(regions[0], "Solar_Wind")
        span = self.whi.attributes["MISSION_TIME_SPAN"]
        self.assertIsInstance(span, tuple)
        self.assertEqual(len(span), 2)
        self.assertEqual(span[0].year, 2000)
        self.assertEqual(span[1].year, 2026)

    def test_values_outlive_file(self):
        values = cefast.load(corpus_file("C1_CP_FGM_SPIN"))[
            "B_vec_xyz_gse__C1_CP_FGM_SPIN"
        ].values
        gc.collect()
        self.assertEqual(values.shape, (286, 3))
        self.assertTrue(np.isfinite(values.sum()))


class TestProcesses(unittest.TestCase):
    @unittest.skipUnless(sys.platform.startswith("linux"), "fork is POSIX only")
    def test_fork_after_parallel_load(self):
        # The worker pool is process-wide and a forked child inherits none of its threads, so the
        # child must rebuild it instead of waiting on workers that do not exist there.
        path = corpus_file("C1_CP_WHI_ELECTRON_DENSITY")
        before = cefast.load(path, threads=4)

        pid = os.fork()
        if pid == 0:
            try:
                after = cefast.load(path, threads=4)
                same = list(before) == list(after) and all(
                    np.array_equal(before[name].values, after[name].values)
                    for name in before
                )
                os._exit(0 if same else 1)
            except BaseException:
                os._exit(2)

        deadline = time.monotonic() + 30
        while True:
            done, status = os.waitpid(pid, os.WNOHANG)
            if done == pid:
                break
            if time.monotonic() > deadline:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
                self.fail("the forked child did not finish within 30 s")
            time.sleep(0.01)
        self.assertEqual(os.waitstatus_to_exitcode(status), 0)


class TestDocstrings(unittest.TestCase):
    """The API reference page of the documentation is generated from these docstrings."""

    def test_public_objects_are_documented(self):
        objects = {
            "cefast": cefast,
            "load": cefast.load,
            "ParseError": cefast.ParseError,
            "CEF": cefast.CEF,
            "Variable": cefast.Variable,
        }
        objects.update({f"CEF.{name}": getattr(cefast.CEF, name) for name in ("keys", "attributes")})
        objects.update(
            {
                f"Variable.{name}": getattr(cefast.Variable, name)
                for name in ("name", "value_type", "sizes", "is_nrv", "attributes", "values")
            }
        )
        undocumented = [name for name, obj in objects.items() if not has_prose(obj.__doc__)]
        self.assertEqual(undocumented, [])


REPR_FILE = b"""FILE_NAME = "demo.cef"
FILE_FORMAT_VERSION = "CEF-2.0"
END_OF_RECORD_MARKER = "$"
START_META = FILE_TIME_SPAN
   VALUE_TYPE = ISO_TIME_RANGE
   ENTRY = 2005-06-10T00:00:00Z/2005-06-10T02:00:00Z
END_META = FILE_TIME_SPAN
START_META = MISSION_DESCRIPTION
   ENTRY = "The aim of the Cluster mission is to study small-scale structures of the magnetosphere"
   ENTRY = "and its environment in three dimensions."
END_META = MISSION_DESCRIPTION
START_VARIABLE = time_tags
  VALUE_TYPE = ISO_TIME
  UNITS = "s"
END_VARIABLE = time_tags
START_VARIABLE = B_vec
  VALUE_TYPE = FLOAT
  SIZES = 3
  UNITS = "nT"
  CATDESC = "Magnetic field vector"
  FILLVAL = -1.0E31
  DEPEND_0 = time_tags
END_VARIABLE = B_vec
START_VARIABLE = label
  VALUE_TYPE = CHAR
  SIZES = 3
  DATA = "Bx", "By", "Bz"
END_VARIABLE = label
DATA_UNTIL = EOF
2005-06-10T00:00:00Z, 1, 2, 3 $
2005-06-10T00:00:04Z, 4, 5, 6 $
"""


class TestShape(unittest.TestCase):
    def test_shape_is_the_shape_of_values(self):
        for dataset in ("C1_CP_FGM_SPIN", "C3_CP_PEA_LERL_DEFlux", "CC_CP_AUX_MAARBLE_KIR_ULF_PC35"):
            f = cefast.load(corpus_file(dataset))
            for name in f:
                self.assertEqual(f[name].shape, f[name].values.shape, (dataset, name))

    def test_shape_is_documented(self):
        self.assertTrue(has_prose(cefast.Variable.shape.__doc__))


class TestRepr(unittest.TestCase):
    def setUp(self):
        self.f = cefast.load(REPR_FILE)

    def test_file_repr_names_the_file_and_counts(self):
        lines = repr(self.f).splitlines()
        self.assertEqual(lines[0], "CEF file demo.cef")
        self.assertEqual(lines[1], "2 records, 3 variables")

    def test_file_repr_lists_variables_with_type_shape_units_and_description(self):
        text = repr(self.f)
        rows = {line.split()[0]: line for line in text.splitlines() if line.startswith("  ")}
        self.assertEqual(rows["B_vec"].split()[1:5], ["FLOAT", "(2,", "3)", "nT"])
        self.assertTrue(rows["B_vec"].endswith("Magnetic field vector"))
        self.assertIn("header", rows["label"])
        self.assertLess(text.index("Variables:"), text.index("Attributes:"))

    def test_file_repr_shows_times_as_iso_and_cuts_long_attributes(self):
        text = repr(self.f)
        self.assertIn("  FILE_TIME_SPAN: 2005-06-10T00:00:00/2005-06-10T02:00:00", text)
        description = next(line for line in text.splitlines() if "MISSION_DESCRIPTION" in line)
        self.assertTrue(description.endswith("…"))
        self.assertLessEqual(max(len(line) for line in text.splitlines()), 100)

    def test_variable_repr(self):
        lines = repr(self.f["B_vec"]).splitlines()
        self.assertEqual(lines[0], "B_vec: FLOAT, shape (2, 3), record-varying")
        self.assertIn("  UNITS: 'nT'", lines)
        self.assertIn("  FILLVAL: -1e+31", lines)
        self.assertEqual(repr(self.f["label"]).splitlines()[0],
                         "label: CHAR, shape (3,), non-record-varying")

    def test_str_is_repr(self):
        self.assertEqual(str(self.f), repr(self.f))
        self.assertEqual(str(self.f["B_vec"]), repr(self.f["B_vec"]))

    def test_file_without_name_or_records(self):
        header_only = REPR_FILE.replace(b'FILE_NAME = "demo.cef"\n', b"").split(b"2005-06-10T00:00:00Z, 1")[0]
        header_only = header_only.replace(b"START_VARIABLE = time_tags", b"START_VARIABLE = time_tags\n  DATA = 2005-06-10T00:00:00Z")
        header_only = header_only.replace(b"  DEPEND_0 = time_tags\n", b"  DATA = 1, 2, 3\n")
        lines = repr(cefast.load(header_only)).splitlines()
        self.assertEqual(lines[0], "CEF file")
        self.assertEqual(lines[1], "3 variables")


def has_prose(doc):
    """pybind11 writes a `name(self: ...) -> type` signature line into every docstring."""
    signature = re.compile(r"^\w+\(.*\)( -> .*)?$")
    lines = (line.strip() for line in (doc or "").splitlines())
    return any(line and not signature.match(line) for line in lines)


if __name__ == "__main__":
    unittest.main()
