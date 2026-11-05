import gc
import gzip
import os
import pathlib
import unittest

import numpy as np

import cefast

RESOURCES = pathlib.Path(os.environ["CEFAST_TEST_RESOURCES"])
CORPUS = RESOURCES / "csa"
FIXTURES = RESOURCES / "fixtures"

WHI = CORPUS / "C1_CP_WHI_ELECTRON_DENSITY__20020202_000000_20020202_120000_V100209.cef"
CODIF = CORPUS / "C1_CP_CIS-CODIF_HS_H1_CS__20020202_000000_20020202_003000_V151020.cef"
EFW = CORPUS / "C1_CP_EFW_L3_E__20050610_000000_20050610_002000_V220603.cef"
FGM = CORPUS / "C1_CP_FGM_SPIN__20050610_000000_20050610_002000_V140305.cef"
GRMB = CORPUS / "C3_CT_AUX_GRMB__20050101_000000_20050131_000000_V261005.cef"
EDI = CORPUS / "C1_CP_EDI_MP__20050610_000000_20050610_001000_V180117.cef"

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
        from_path = cefast.load(WHI)
        from_bytes = cefast.load(WHI.read_bytes())
        self.assertEqual(list(from_path), list(from_bytes))
        for name in from_path:
            np.testing.assert_array_equal(
                from_path[name].values, from_bytes[name].values
            )
        self.assertEqual(from_path.attributes, from_bytes.attributes)

    def test_load_gz_bytes(self):
        loaded = cefast.load(gzip.compress(FGM.read_bytes()))
        plain = cefast.load(FGM)
        self.assertEqual(list(loaded), list(plain))
        self.assertEqual(loaded["B_vec_xyz_gse__C1_CP_FGM_SPIN"].values.shape, (286, 3))

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

    def test_missing_key(self):
        loaded = cefast.load(FGM)
        self.assertNotIn("nope", loaded)
        with self.assertRaises(KeyError):
            loaded["nope"]


class TestVariables(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.whi = cefast.load(WHI)

    def test_time_dtype(self):
        values = self.whi["time_tags__C1_CP_WHI_ELECTRON_DENSITY"].values
        self.assertEqual(values.dtype, np.dtype("datetime64[ns]"))
        self.assertEqual(values.shape, (24410,))
        self.assertEqual(values[-1], np.datetime64("2002-02-02T11:57:13.320"))

    def test_multidim_shape(self):
        values = cefast.load(CODIF)["3d_ions__C1_CP_CIS-CODIF_HS_H1_CS"].values
        self.assertEqual(values.shape, (149, 8, 16, 31))
        self.assertEqual(values[0, 0, 0, 0], 0.0)
        self.assertEqual(values[0, 0, 0, 1], 0.0)

    def test_nrv_shape(self):
        theta = cefast.load(CODIF)["theta__C1_CP_CIS-CODIF_HS_H1_CS"]
        self.assertTrue(theta.is_nrv)
        self.assertEqual(theta.sizes, (8,))
        self.assertEqual(theta.values.shape, (8,))
        self.assertEqual(theta.values[0], -78.75)

    def test_nrv_scalar_shape(self):
        sta = CORPUS / "C1_PP_STA__20050610_000000_20050610_002000_V090213.cef"
        half_interval = cefast.load(sta)["half_interval__C1_PP_STA"]
        self.assertTrue(half_interval.is_nrv)
        self.assertEqual(half_interval.sizes, (1,))
        self.assertEqual(half_interval.values.shape, (1,))
        self.assertEqual(half_interval.values[0], 2.0)

    def test_varying_scalar_shape(self):
        time = cefast.load(FGM)["time_tags__C1_CP_FGM_SPIN"]
        self.assertFalse(time.is_nrv)
        self.assertEqual(time.sizes, (1,))
        self.assertEqual(time.values.shape, (286,))

    def test_char_values(self):
        spectrum = self.whi["Spectrum_Type__C1_CP_WHI_ELECTRON_DENSITY"].values
        self.assertEqual(spectrum.dtype.kind, "U")
        self.assertEqual(spectrum.shape, (24410,))
        self.assertEqual(spectrum[-1], "A")

    def test_range_shape(self):
        values = cefast.load(GRMB)["time_tags__C3_CT_AUX_GRMB"].values
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
        edi = cefast.load(EDI)["Half_interval__C1_CP_EDI_MP"]
        self.assertEqual(edi.value_type, "DOUBLE")
        self.assertEqual(edi.values.dtype, np.float64)
        self.assertEqual(
            cefast.load(EFW)["E_bitmask__C1_CP_EFW_L3_E"].values.dtype, np.int32
        )

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
        values = cefast.load(FGM)["B_vec_xyz_gse__C1_CP_FGM_SPIN"].values
        gc.collect()
        self.assertEqual(values.shape, (286, 3))
        self.assertTrue(np.isfinite(values.sum()))


if __name__ == "__main__":
    unittest.main()
