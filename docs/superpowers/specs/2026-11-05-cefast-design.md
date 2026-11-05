# CEFast — a fast, maintained CEF reader (design)

Date: 2026-11-05. Status: draft for approval.

## Intent

CEF (Cluster Exchange Format) is the text format of ESA's Cluster Science Archive (CSA).
The only real reader today is IRAP's CEFLIB. It is old, hard to install (source tarball, no
PyPI wheels), and its maintainer has passed away.

We want a reader people can rely on for years:

- correct on every valid CEF file,
- as fast as the hardware allows,
- `pip install`-able on every platform, like pycdfpp,
- small and readable enough that someone else could maintain it.

Two users drive it:

1. **speasy** — a CEF codec next to the CDF one, to reach CSA datasets that have no ISTP CDF
   version and to skip the server-side conversion.
2. **The Cluster community** — anyone reading CSA files from Python.

## Goals

- Read the full CEF 2.0 format (see "Format coverage").
- Read `.cef` and `.cef.gz`, from a path or from bytes in memory.
- Python API that feels like pycdfpp: load a file, get variables by name, numpy arrays,
  attributes as Python values, times as `datetime64[ns]`.
- Throughput limited by gzip decompression on compressed files, and close to memory/parse
  bandwidth on uncompressed ones.
- Wheels on PyPI for the same matrix as pycdfpp.

## Non-goals

- **Writing CEF.** Not now. Keep the in-memory model simple enough that a writer could be
  added later, but write no code for it.
- **Integration into pycdfpp / CDFpp.** CEFast is standalone. speasy already handles many
  codecs; that is where formats meet. (User decision.)
- **CEF→CDF conversion.** Out of scope.
- **Metadata validation against the CSA Meta Data Dictionary** (what CEFMDD does). We parse
  what is there; we do not judge it beyond what's needed to read data.

## Anti-goals

- Becoming the next abandoned library: no clever code that only its author can follow.
  Every SIMD fast path has a scalar twin, tested against each other.
- Silently wrong numbers. A malformed file raises a clear error naming the line/record;
  it never yields shifted columns or garbage values.

## Architecture (decided)

Three repos, dependencies flowing one way only:

```
cpp_utils  <──  CEFast (new)        speasy ── uses ──> cefast (Python wheel)
    ^
    └──────  CDFpp (unchanged behaviour)
```

1. **cpp_utils** gains a new `cpp_utils_python/` folder (sibling of `cpp_utils_qt/`, same
   opt-in-by-include model: only code that includes it needs pybind11/CPython). It starts with
   the generic helpers now in `CDFpp/pycdfpp/` that CEFast needs (user decision: start small,
   grow when a second consumer such as space_ouija needs more):
   - numpy allocation without zero-fill (`fast_allocate_array`) + owner keep-alive capsule;
   - GIL helpers (`without_gil`, interpreter-exit guard);
   - time conversions: C++ time point ↔ `datetime.datetime`, ns ↔ `datetime64[ns]`
     (the generic part of `pycdfpp/chrono.hpp`, ~100 lines; the CDF EPOCH/TT2000 parts stay).
   Not moved yet: list/tuple browsing (write path only), fsspec range-reader bridge, string
   helpers. pycdfpp is switched to the moved helpers; pycdfpp headers are not installed, so no
   public API changes. Full CDFpp test suite must pass.
   **Gzip is not moved** (user decision): CDFpp's wrappers need the uncompressed size up
   front and stop after the first gzip member; CDF knows block sizes, `.cef.gz` does not.
2. **CEFast** — new repo. Header-only C++20 library + `cefast` pybind11 module. Depends on
   cpp_utils, fast_float, zlib and/or libdeflate, xsimd if needed. Meson + meson-python,
   same layout and CI style as CDFpp. Owns a streaming gzip reader (unknown output size,
   multi-member files, truncation is an error). Moves to cpp_utils when a second user appears.
3. **speasy** — a CEF codec using cefast (separate, later PR in speasy).

Language: C++20 (my recommendation, accepted): reuse of cpp_utils, fast_float, simdjson-style
techniques, and the existing meson/cibuildwheel tooling.

## Format coverage

Everything in CEF 2.0 a reader can meet. The builder must collect the spec (CSA document
"CEF version 2.0", CAA-EST-FS-007 or its successor; the CAA user guide CAA-EST-UG-0001 has a
summary) and list every construct in a coverage table in the repo. At least:

- Header: `KEYWORD = value` lines, comments (`!`), line continuation (`\`), quoted strings,
  multi-value entries (comma-separated).
- `INCLUDE = "file.ceh"` (quoted or not), recursive, resolved against: a user resolver
  callable `name -> bytes | None`, then the file's directory, then user `include_paths`, then
  the env var CEFLIB uses (builder to check its name). Needed because speasy loads from bytes.
  Missing include = error naming the file.
- Global metadata blocks `START_META … END_META` (with `ENTRY` and `VALUE_TYPE`).
- Variable blocks `START_VARIABLE … END_VARIABLE`: `SIZES` (multi-dim), `VALUE_TYPE`
  (`FLOAT`, `DOUBLE`, `INT`, `BYTE`, `ISO_TIME`, `ISO_TIME_RANGE`, `CHAR`), `FILLVAL`, `DATA`
  for non-record-varying values, and every other attribute kept as metadata.
- Data section: `DATA_UNTIL` (incl. `EOF`), `END_OF_RECORD_MARKER` (default newline,
  often `$`), field separator `,`, comments and blank lines inside data, quoted strings.
- Array ordering of multi-dim `SIZES` in records: builder must confirm from the spec
  and from CEFLIB, and test it on a real spectrogram file.
- ISO time with variable fractional precision (none up to ns), `Z` suffix; time ranges
  (`start/stop`) as an `(N, 2)` `datetime64[ns]` array.
- `SIZES = 1` means scalar. `DEPEND_i`, `DELTA_PLUS/MINUS` stay attributes naming other
  variables (as in ISTP CDF; speasy already resolves those by name). No axis resolution at load.

**Real files are a superset of the spec** (CSA files use unquoted `INCLUDE`, unquoted `$`
markers, unquoted `ENTRY` values, `ISO_TIME_RANGE` absent from the spec's type lists). The
repo keeps an **accepted-deviations table**, one test per row. Strictness is kept where a
mistake would shift values: field count per record, unparseable number, unterminated quote —
those raise with file/line/record.

**CEF version:** CEF-2 only. The CSA probe (2026-11-05, 41 files / 39 datasets, 2000–2026,
Cluster, Double Star, ground-based) found only `CEF-2.0`; CSA never served CEF-1. A file
without `FILE_FORMAT_VERSION` or with CEF-1 raises a named error, no best effort.
Report: `2026-11-05-csa-cef-probe.md` (this folder), samples in `~/.cache/cefast-csa-probe/corpus/`.

**Facts from the probe the builder should use:**
- CSA regenerates headers at download and inlines `INCLUDE`s, so speasy downloads have none;
  `INCLUDE` matters for raw archive/producer files (e.g. STAFF `.ceh` headers).
- Every CSA file ends with `!RECORDS= N` then `END_DATA` (`DATA_UNTIL = END_DATA`). Use
  `!RECORDS` as a pre-allocation hint only, never trusted: verify the count, fall back to
  growing.
- Record markers seen: newline, unquoted `$`, `;`. `SIZES` with spaces (`"64, 6, 32"`).
  Empty data sections and 1-record files exist.
- Types in the whole CSA: FLOAT 22912, INT 9243, ISO_TIME 2303, CHAR 699, ISO_TIME_RANGE 234,
  DOUBLE 14, BYTE 0. BYTE stays supported (spec) but tested with fixtures only.

**Fill values** are kept as stored (numbers raw, `FILLVAL` exposed as an attribute). Time
fills such as `9999-99-99T99:99:99Z` become `NaT` (datetime64 can't hold them otherwise).

## Performance approach (left to the builder, with a mandated order)

Correct first, fast second, and measured at every step:

1. Scalar reference parser: header parser + record parser using `fast_float` for floats and a
   hand-written integer/ISO-time parser. This is the oracle for everything after.
2. Only if profiling on real files shows parsing (not gzip) dominates: parallel parse — split
   the data section at record markers into chunks, allocate output arrays once (no zero-fill),
   parse chunks in parallel into their slices (cpp_utils `parallel_chunks`).
3. Same gate: simdjson-style SIMD structural indexing
   (bitmasks for separators/markers/quotes/comments), and SIMD fixed-width ISO-time parsing.
   Each fast path keeps its scalar twin and a test comparing them on every test file.

Benchmarks on real CSA files, reported against CEFLIB's Python module. Gzip decompression
time is reported separately so parse speed is visible.

## Python API (shape decided, details left to builder)

Feels like pycdfpp:

```python
import cefast
f = cefast.load("C1_CP_FGM_SPIN__20010201_000000_20010202_000000_V140305.cef.gz")
f.attributes["FILE_TITLE"]          # global metadata
v = f["B_vec_xyz_gse__C1_CP_FGM_SPIN"]
v.values                            # numpy array, shape (records, *SIZES)
v.is_nrv                            # non-record-varying: shape is SIZES only
v.attributes["UNITS"], v.attributes["FILLVAL"]   # plain Python values
f["time_tags__C1_CP_FGM_SPIN"].values.dtype   # datetime64[ns]
cefast.load(bytes_obj)             # in-memory
cefast.load(path, include_paths=[...], include_resolver=lambda name: ...)
```

Attributes are plain Python values (lists for multi-value entries, `datetime` for ISO_TIME),
not wrapper objects. Numeric and time arrays are views on C++ buffers (no extra copy after
parsing); CHAR variables are variable-width in real files and come out as copied numpy string
or object arrays. Loading releases the GIL.

## Testing

- **Test corpus:** real files pulled from the CSA archive (user decision), varied across
  instruments: at least FGM (vectors), a spectrogram (multi-dim SIZES, e.g. STAFF or PEACE),
  CIS or CODIF (moments), EFW, a file using `INCLUDE`, a `$` record-marker file, a file with
  `ISO_TIME_RANGE`, a CHAR variable. Keep small files in git; fetch large ones in a test
  fixture with checksum, skipped offline.
- **Authority:** hand-made fixture files with known expected values (from the spec, Python
  `float()` and `datetime`). This is the only way to test ns times and BYTE.
- **Differential check:** CEFLIB (pinned build, one file per process — it is a global
  singleton). It strips `__<DATASET_ID>` from names, rounds floats loosely, can't read BYTE,
  and keeps times as ms doubles, so the comparison normalises names and uses tolerances.
  It is also the speed baseline.
- **Hand-made edge-case files** for what real files lack: malformed records, wrong field
  counts, truncated gzip, unterminated quotes, missing INCLUDE, empty data section, CRLF
  line endings.
- **Fuzzing** of the header and record parsers (libFuzzer, short run in CI).
- Sanitizers (ASan/UBSan/TSan) in CI, like CDFpp.
- TDD: each feature starts with a failing test.

## Packaging & CI

Mirror CDFpp: meson-python, cibuildwheel, same Python and platform matrix (incl. free-threaded
builds, Windows ARM64, macOS arm64), PyPI publish on GitHub release. Repo under the SciQLop
GitHub org (builder to confirm with user at push time; pushing needs explicit consent).

## Work process (user request)

- opencode (via Herdr, `herdr agent prompt`) does most implementation tasks to save tokens.
- Claude writes the plan, reviews every opencode change against the plan and the tests, and
  pushes back where it disagrees. opencode reviews Claude's design and plan the same way.
  Disagreements are surfaced to the user, not silently resolved.
- One build/test invocation per build directory at a time, foreground, for both agents.

## Order of work

1. cpp_utils: create `cpp_utils_python/`; switch pycdfpp to it; CDFpp tests green.
2. CEFast: repo skeleton, streaming gzip reader, test corpus (seeded by the CSA probe) +
   fixtures + CEFLIB differential harness.
3. Header parser (incl. INCLUDE, metadata), then scalar data parser, then Python API.
4. Benchmarks + profiling; parallel parse and SIMD only where profiling says so.
5. Wheels/CI, docs, release.
6. speasy codec.

## Calls I made (check these)

- Repo **CEFast**, Python package **cefast** (user proposal; free on PyPI as of 2026-11-05).
- One `cpp_utils_python/` folder for both pybind11 and CPython helpers.
- Times as `datetime64[ns]`, leap seconds ignored (as numpy does).
- `ISO_TIME_RANGE` as an `(N, 2)` datetime64 array.
- Hand-made fixtures are the authority; CEFLIB is a tolerant differential check + speed baseline.
- Attributes as plain Python values; `is_nrv` flag on variables.
- Fill values kept raw; time fills → NaT.
- INCLUDE resolver callable for in-memory loads.
- Parallel parse gated on profiling, like SIMD.
- Accepted-deviations table for real-file departures from the spec.
- This document becomes the first commit of the new repo once approved.
