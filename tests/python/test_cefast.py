import gc
import gzip
import os
import pathlib
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


if __name__ == "__main__":
    unittest.main()
