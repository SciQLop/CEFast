# Review: cpp_utils_python + CEFast reader plans

Checked every literal expectation against DS-QMW-TN-0010 (iss. 2), CEFLIB 1.8.6 sources, and the
41-file CSA corpus in `~/.cache/cefast-csa-probe/corpus`. Ranked by cost if not fixed.

## 1. Task 7 (Global Constraints + spot checks) / Task 8: the 1 MB corpus rule deletes the files the tests are written against
Corpus sizes: `C1_CP_CIS-CODIF_HS_H1_CS` 1.70 MB, `C1_CP_WHI_ELECTRON_DENSITY` 1.87 MB,
`C1_CP_CIS-CODIF_HS_He2_PEF` 1.26 MB. Task 7 asks for CODIF `3d_ions` `{records,8,16,31}` and the WHI
last record; Task 8 for `(n,8,16,31)` and `11:57:13.320`. None of those files is copied, and
`every_corpus_file_loads` only globs `tests/resources/csa/*.cef`, so the spot checks have no data.
This also drops the design's mandated "CIS or CODIF (moments)" instrument class.
**Fix:** cap at 2 MB (+3.5 MB repo) or download shorter CS/WHI intervals. If 1 MB is hard, retarget the
checks to `C4_CP_WHI` (0.02 MB) and `C3_CP_PEA_LERL_DEFlux` (0.83 MB, `8,3,16`) and drop `(n,8,16,31)`.

## 2. Task 5: non-record-varying `SIZES = 1` must be `(1,)`, not 0-d
`Compute_numpy_sizes` (PYTHON/src/ceflib.c:453-472) sets `ndim = nb_size` unconditionally for
non-varying; `IS_SCALAR` is applied only in the varying branch. Corpus has 11+ such variables
(`half_interval`, `Mode_Sensor` ×3, `STATION_NAME`), so Task 9's shape check fails on almost every
file. Task 6 also never states the varying case: FGM `time_tags … SIZES = 1` is `(nrec,)`.
**Fix:** NRV → shape `sizes` always; varying → `(records,)` if `sizes=={1}` else `(records,*sizes)`;
add `nrv_scalar_shape` and `varying_scalar_shape` tests.

## 3. Task 3/6: header end by "no `=`", data end by exact equality — both wrong
A header line missing `=` (or a `START_VARIABLE` name line) is silently taken as the first data
record → shifted columns, the anti-goal. CEFLIB ends the header only at `DATA_UNTIL`
(C/src/CEF.c:1395-1402) and all 41 CSA files have `DATA_UNTIL = END_DATA`. Conversely Task 6
requires the terminator line to *equal* `data_until`, while the spec says "found at the beginning of
a line" (cefspec.txt:156-163) and CEFLIB prefix-matches, so `END_DATA  ! end` becomes a data record.
**Fix:** end the header at `DATA_UNTIL`/EOF (error on `=`-less lines before it); match `data_until` as
a trimmed prefix.

## 4. Task 4: an error inside an included `.ceh` cannot name the right file
`header_line{key, values, line}` carries no source, so `parse_error(source, line, …)` reports the
including CEF's name with the include file's line number — against "error naming the line/record".
Same task also introduces `schema::nrv_data` that its own `schema` interface omits.
**Fix:** add a source identity to `header_line`, thread it into `parse_error`, add `nrv_data` to the
struct, test an error raised from `nested.ceh`.

## 5. Task 9: the differential will report false attribute mismatches
CEFLIB applies `Prefixe()` to every variable attribute value, not just names (CEF.c:1345, :431), so
`DEPEND_1 = theta__DS` becomes `theta` on its side. Its Python `gattrs()` exposes only
FILE_NAME/FILE_FORMAT_VERSION/END_OF_RECORD_MARKER/DATA_UNTIL, never the metadata blocks.
The plan only says "compare on stripped names".
**Fix:** strip the suffix in attribute values too, compare `gattrs()` as that 4-key subset, and limit
variable-attribute comparison to SIZES/VALUE_TYPE/FILLVAL/DEPEND_i; list the rest as CEFLIB quirks.

## 6. Task 7 `gz_equals_plain` needs compression that Task 2 never provides
`gzip.hpp` declares only `gunzip`; no interface has a compressor, yet the test is told to gzip a
corpus file with zlib in-process.
**Fix:** add `std::string gzip(std::string_view)` (`deflateInit2`, `15+16` windowBits, ~25 lines) with
its own unit test, or gzip the file offline into `tests/resources/fixtures/`.

## 7. Task 4: the untyped attribute cascade loses text and buys nothing
"int64 → double → iso_time → range → string" mangles metadata: `VERSION_NUMBER` ENTRY `090213`
becomes `90213` (C1_PP_STA), `ENTRY = 01` becomes `1`. CEFLIB keeps entry text unless a `VALUE_TYPE`
is in force (CEF.c:1283-1289), which is also what the design asks ("parse what is there").
**Fix:** store attribute entries as strings; convert only under an explicit `VALUE_TYPE`. Keeps
`MISSION_REGION` (strings) and `MISSION_TIME_SPAN` (typed range) as the tests expect and removes four
conversion branches and their error paths.

## 8. Task 4 checks `END_META` names but not `END_VARIABLE` names
An unclosed/misnamed variable block silently absorbs the following attributes into `variable.attrs`
— schema shift with no error, while the META case already raises. Spec §2.7 requires
`END VARIABLE = name`; variables are the case-sensitive exception, so compare exactly.
**Fix:** same-name check plus "START_VARIABLE while a block is open is an error"; test both.

## 9. cpp_utils Task 1 cannot be built as specified
Building a pybind11 test module needs pybind11 *and* numpy C headers. cpp_utils has no
`pybind11.wrap` (its `subprojects/` holds only Catch2/hedley/bshoshany) and meson has no
`dependency('numpy')`, so `meson setup -Dpython=true build-py` fails at setup — Step 2 cannot show
the intended red.
**Fix:** add `subprojects/pybind11.wrap`, `dependency('pybind11', required: get_option('python'))`,
numpy include via `run_command(python3, '-c', 'import numpy; print(numpy.get_include())', check: true)`,
and guard `tests/python` on the option.

## 10. No dtype table, and the chosen dtypes differ from the reference reader
`column` stores INT as `int64_t`, but CEF INT is a 4-byte int (CEFLIB maps it to NPY_INT, ceflib.c:537;
pycdfpp maps CDF_INT4 to int32): 9243 INT parameters archive-wide pay 2× memory. BYTE is `uint8_t` here,
`char` in CEFLIB, so a BYTE `FILLVAL = -1` would be rejected by "0–255". Task 9 compares values only.
**Fix:** state the table (FLOAT float32, DOUBLE float64, INT int32, BYTE int8 accepting 0–255 on input,
CHAR str_, time datetime64[ns]) and assert dtypes in Task 8.

## 11. Coverage gaps against the design and the spec
BYTE has no positive test (design: fixtures only) — Task 5 only tests `256 → error`. No end-to-end
ISO_TIME_RANGE *variable* assertion: GRMB loads in Task 7 but nothing checks `(nrec,2)`, dtype or NaT.
Nothing tests `\` continuation inside a variable's `DATA` (§2.7; two corpus files use continuation),
and `float_empty_raises` duplicates Task 6's record-level `empty_field_raises`.
**Fix:** add `byte_values` (0/1/127/255 + fill), `range_variable` (shape, values, NaT), and
`nrv_data_continuation`; move/delete the duplicate.

## 12. cpp_utils Task 1's interface does not match the code it claims to move
`fast_allocate_array` is declared `(const Shape&)` but the source is
`(const Shape&, Owner&& = nullptr)` (CDFpp/pycdfpp/collections.hpp:132-154) and the task text says
"shape + optional owner". `view_with_owner` does not exist in pycdfpp at all, so "moved" hides that it
is new code. No test asserts the views are writable, the one property a writable zero-copy view needs.
**Fix:** keep the `owner` default parameter, declare `view_with_owner` as new, and add
`self.assertTrue(a.flags.writeable)` plus a mutation check to `test_view_values_and_shape`.
