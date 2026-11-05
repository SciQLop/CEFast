# CEFast Reader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A correct, complete CEF-2 reader: header-only C++20 library + `cefast` Python module, loading every file of the CSA probe corpus.

**Architecture:** Bytes in (file or memory, gzip auto-detected) → header lexer → header parser building a schema (globals, variables, INCLUDE resolved) → scalar record parser filling typed column buffers → `cef_file`. Python wraps `cef_file` with zero-copy numpy views. Performance work (parallel, SIMD, libdeflate), CEFLIB speed baseline, wheels/CI and the speasy codec are later plans.

**Tech Stack:** C++20, meson ≥ 1.2, Catch2 v3, fast_float, zlib, cpp_utils (incl. `cpp_utils_python/` from plan `2026-11-05-cpp-utils-python.md`), pybind11, numpy, unittest.

**Spec:** `docs/superpowers/specs/2026-11-05-cefast-design.md`. CEF facts with citations: `docs/superpowers/specs/2026-11-05-cef-facts.md`. Corpus survey: `docs/superpowers/specs/2026-11-05-csa-cef-probe.md`.

## Global Constraints

- Repo `~/Documents/prog/CEFast`, branch `main` for now (empty repo). Never push without user consent.
- C++20, header-only under `include/cefast/`; `.clang-format` copied from CDFpp (WebKit-based, 100 cols, 4-space, Allman).
- Namespace `cefast`. Python package `cefast` (extension `_cefast` + `cefast/__init__.py`).
- CEF-2 only: missing `FILE_FORMAT_VERSION` or a value not starting with `CEF-2` → `parse_error` naming the found value.
- Times: `int64` ns since 1970-01-01T00:00:00Z, leap seconds ignored. Python: `datetime64[ns]`.
- Fill values kept raw. Exception: an ISO_TIME/ISO_TIME_RANGE data token textually equal to the variable's `FILLVAL` token → NaT (`INT64_MIN`).
- Errors: `cefast::parse_error : std::runtime_error`, message `"<source>:<line>: <what>"` (+ `" (record <n>)"` in data). Python: `cefast.ParseError(ValueError)`.
- Never silently produce shifted columns: wrong field count, unparseable number, unterminated quote, empty field → error.
- One build/test invocation per build directory at a time, foreground. No backgrounded builds.
- Test corpus: copy every `~/.cache/cefast-csa-probe/corpus/**/*.cef` (all ≤ 2 MB, ~11 MB total) into `tests/resources/csa/` (flat, keep file names).
- Dtypes: FLOAT→float32, DOUBLE→float64, INT→int32 (out of range → error), BYTE→uint8 (0–255, else error; no CSA file uses BYTE), CHAR→numpy `str_` (copied), ISO_TIME→datetime64[ns], ISO_TIME_RANGE→datetime64[ns] with trailing dim 2.
- Shapes: record-varying → `(records,)` if `sizes == {1}` else `(records, *sizes)`; non-record-varying → `sizes` always (so `SIZES = 1` → `(1,)`, as CEFLIB); range adds a trailing `2`.

## Review Focus

1. Record marker `;` or `$` with records spanning several lines and a `!` comment line between them — Task 6 test `multi_line_records_with_comment`.
2. A `"` or `,` or `!` inside a quoted CHAR value in data — Task 6 test `quoted_char_with_separators`.
3. CRLF line endings everywhere (header and data) — Task 3 and Task 6 tests `crlf`.
4. Empty data section (header only, `!RECORDS= 0`) and 1-record files — Task 6 test `empty_and_single_record`; corpus has both.
5. `!RECORDS=` hint lying (bigger or smaller than the real count) — Task 6 test `records_hint_is_only_a_hint`.

---

### Task 1: Skeleton + ISO time parser

**Files:**
- Create: `meson.build`, `meson_options.txt` (`with_tests` default true, `python` default true), `.clang-format`, `.gitignore`, `subprojects/{catch2,fast_float,zlib,pybind11,cpp_utils}.wrap` (copy CDFpp's wraps where they exist; `fast_float` from wrapdb), `include/cefast/time.hpp`, `include/cefast/errors.hpp`, `tests/meson.build`, `tests/cpp/test_time.cpp`
- Local dev: `subprojects/cpp_utils` may be a symlink to `~/Documents/prog/cpp_utils` (gitignored) until cpp_utils is pushed.

**Interfaces:**
- Produces:
  - `struct parse_error : std::runtime_error` (in `errors.hpp`), ctor `parse_error(std::string_view source, std::size_t line, std::string_view what, std::optional<std::size_t> record = {})`.
  - `std::optional<int64_t> parse_iso_time(std::string_view s) noexcept` — `nullopt` if invalid.
  - `std::optional<std::array<int64_t, 2>> parse_iso_time_range(std::string_view s) noexcept` — `start/stop`.
  - `constexpr int64_t nat = std::numeric_limits<int64_t>::min();`

Grammar (facts Q7): `YYYY-MM-DDThh:mm:ss[.f+]Z`, `T`/`Z` case-insensitive, any number of fraction digits (beyond 9 truncated), seconds mandatory, only `Z`. Calendar-validated (month 1–12, day within month incl. leap years, hh 0–23, mm 0–59, ss 0–60; `:60` adds to the next second by plain arithmetic). Result outside int64 ns range (before 1677-09-22 / after 2262-04-11) → `nullopt`. Surrounding whitespace is the caller's job.

- [ ] **Step 1: Write the failing tests** (`tests/cpp/test_time.cpp`)

```cpp
CHECK(parse_iso_time("1970-01-01T00:00:00Z") == 0);
CHECK(parse_iso_time("2002-02-02T11:57:09.292Z") == 1012651029292000000);
CHECK(parse_iso_time("2002-02-02t11:57:09.292z") == 1012651029292000000);
CHECK(parse_iso_time("1958-01-01T00:00:00.000000001Z") == -378691200000000000 + 1);
CHECK(parse_iso_time("2000-01-01T00:00:00.1234567891234Z") == 946684800123456789);
CHECK(parse_iso_time("2016-12-31T23:59:60Z") == parse_iso_time("2017-01-01T00:00:00Z"));
CHECK(parse_iso_time("2000-02-29T00:00:00Z").has_value());
CHECK_FALSE(parse_iso_time("2001-02-29T00:00:00Z"));
CHECK_FALSE(parse_iso_time("9999-99-99T99:99:99Z"));
CHECK_FALSE(parse_iso_time("9999-12-31T23:59:59Z"));   // valid date, outside ns range
CHECK_FALSE(parse_iso_time("2000-08-09T11:13Z"));      // seconds mandatory
CHECK_FALSE(parse_iso_time("2000-08-09T11:13:00+02:00"));
CHECK_FALSE(parse_iso_time("2000-08-09T11:13:00"));
auto r = parse_iso_time_range("2000-07-16T00:00:00Z/2026-12-31T23:59:59Z");
REQUIRE(r); CHECK((*r)[0] == 963705600000000000);
CHECK_FALSE(parse_iso_time_range("2000-07-16T00:00:00Z"));
```

- [ ] **Step 2: Run to verify it fails** — `meson setup build && meson test -C build time` → FAIL (header missing / compile error).
- [ ] **Step 3: Implement `time.hpp` and `errors.hpp`.** Days-from-civil via `std::chrono::year_month_day` + `sys_days`; fixed-position digit reads; no locale, no `std::get_time`.
- [ ] **Step 4: Run** — `meson test -C build` → all pass.
- [ ] **Step 5: Commit** — `feat: project skeleton and ISO time parser`.

### Task 2: Input layer (file/bytes, streaming gzip)

**Files:**
- Create: `include/cefast/input.hpp`, `include/cefast/gzip.hpp`, `tests/cpp/test_input.cpp`, `tests/resources/fixtures/` (generated `.gz` fixtures committed, produced by a small `tests/resources/make_gz.py` that is also committed)

**Interfaces:**
- Produces:
  - `std::string gzip(std::string_view plain)` — `deflateInit2` with `15 + 16` windowBits; used by tests (and a future writer).
  - `std::string gunzip(std::span<const char> compressed, std::string_view source)` — zlib `inflateInit2(…, 16 + MAX_WBITS)`, output grows by doubling (initial guess: 4× input); multi-member: on `Z_STREAM_END` with input left, `inflateReset` and continue; trailing zero padding after the last member accepted; truncated / corrupt → `parse_error(source, 0, "corrupt or truncated gzip data")`.
  - `struct input_buffer { std::string bytes; std::string source; std::optional<std::filesystem::path> directory; };`
  - `input_buffer read_input(const std::filesystem::path&)` and `input_buffer read_input(std::span<const char> bytes)` — gzip detected by magic `1f 8b`, not by extension; `source` = path string or `"<memory>"`.
  - simplify: whole file read into memory, no mmap/streaming parse; upgrade path is the performance plan.

- [ ] **Step 1: Write the failing tests:** `gzip_roundtrip`, `plain_bytes_passthrough`, `gzip_single_member` (content == original), `gzip_two_members_concatenated` (content == a + b), `gzip_truncated_raises` (`REQUIRE_THROWS_AS(…, parse_error)`), `gzip_with_trailing_zeros`, `missing_file_raises` (`std::filesystem::filesystem_error` or `parse_error`, pick one and test it), `gzip_detected_without_extension`.
- [ ] **Step 2: Run** `meson test -C build input` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `meson test -C build` → all pass.
- [ ] **Step 5: Commit** — `feat: input layer with streaming multi-member gzip`.

### Task 3: Header lexer

**Files:**
- Create: `include/cefast/header/lexer.hpp`, `tests/cpp/test_lexer.cpp`

**Interfaces:**
- Produces:
  - `struct token { std::string text; bool quoted; };`
  - `struct header_line { std::string key; std::vector<token> values; std::size_t line; std::shared_ptr<const std::string> source; };` (`source` = file the line came from, so errors inside an included `.ceh` name that file) (`key` upper-cased, trimmed; `values` comma-split)
  - `struct lexed_header { std::vector<header_line> lines; std::size_t data_offset; std::size_t data_line; };`
  - `lexed_header lex_header(std::string_view text, std::string_view source)` — the header ends right after the `DATA_UNTIL` line (CEFLIB CEF.c:1395-1402; all 41 CSA files have it); `data_offset` = byte offset of the next line. A non-blank, non-comment line without `=` outside quotes before that → `parse_error` ("expected KEY = value"). No `DATA_UNTIL` in the whole text → `parse_error` ("DATA_UNTIL missing"). In an included `.ceh` (Task 4), reaching its end is normal.

Rules (facts Q3–Q6): `!` outside quotes starts a comment to end of line; quoted text is verbatim (no escapes, `!`/`,`/spaces kept), unterminated quote → `parse_error`; whitespace (space, tab, `\r`) around tokens ignored; a line whose last non-comment character is `\` continues on the next line (anywhere in the header: accepted deviation from CEFLIB, which only does it in variable blocks); `\` inside quotes is verbatim; value list split on `,` outside quotes; empty value list allowed (`KEY =`).

- [ ] **Step 1: Write the failing tests:** `key_and_quoted_value` (`FILE_FORMAT_VERSION = "CEF-2.0"` → key `FILE_FORMAT_VERSION`, one quoted token `CEF-2.0`); `unquoted_marker` (`END_OF_RECORD_MARKER = $` → `$`, quoted=false); `comment_after_value`; `bang_and_comma_inside_quotes` (`ENTRY = "a, b ! c"` → one token `a, b ! c`); `continuation` (`DATA = 1, 2, \` + `3` → three tokens); `commented_attribute_skipped` (`! VALIDMIN = -90.0`); `crlf` (same results with `\r\n`); `unterminated_quote_raises` (message contains `:<line>:`); `data_offset_after_data_until` (header ending `DATA_UNTIL = END_DATA` + `2002-02-02T00:00:06.267Z, 8.031,` → `data_offset` points at `2002`); `line_without_equals_raises`; `data_until_missing_raises`; `sizes_with_spaces` (`SIZES = "64, 6, 32"` → kept as one quoted token; the parser splits it, Task 4).
- [ ] **Step 2: Run** `meson test -C build lexer` → FAIL.
- [ ] **Step 3: Implement** a single-pass character scanner (no regex).
- [ ] **Step 4: Run** `meson test -C build` → all pass.
- [ ] **Step 5: Commit** — `feat: header lexer`.

### Task 4: Header parser → schema (incl. INCLUDE)

**Files:**
- Create: `include/cefast/model.hpp`, `include/cefast/header/parser.hpp`, `include/cefast/header/attributes.hpp`, `tests/cpp/test_header.cpp`, `tests/resources/fixtures/include/{main.cef,common.ceh,nested.ceh}`

**Interfaces:**
- Consumes: `lex_header`, `read_input`, `parse_iso_time(_range)`, `parse_error`.
- Produces (`model.hpp`):
  - `enum class value_type { FLOAT, DOUBLE, INT, BYTE, ISO_TIME, ISO_TIME_RANGE, CHAR };`
  - `using scalar = std::variant<std::string, int64_t, double, iso_time, iso_time_range>;` with `struct iso_time { int64_t ns; }; struct iso_time_range { int64_t start, stop; };`
  - `struct attribute { std::string name; std::vector<scalar> entries; };`
  - `using attributes = std::vector<attribute>;` (file order; lookup helper `const attribute* find(const attributes&, std::string_view name)`)
  - `using column = std::variant<no_init_vector<float>, no_init_vector<double>, no_init_vector<int32_t>, no_init_vector<int64_t>, no_init_vector<uint8_t>, std::vector<std::string>>;` (`no_init_vector` = `cpp_utils::containers::no_init_vector`; ISO_TIME and ISO_TIME_RANGE use `int64_t`, range stores start,stop pairs)
  - `struct variable { std::string name; value_type type; std::vector<std::size_t> sizes; bool is_nrv; std::size_t records; column values; attributes attrs; std::string fillval_token; };`
  - `struct cef_file { attributes globals; std::vector<variable> variables; };`
- Produces (`parser.hpp`):
  - `struct load_options { std::vector<std::filesystem::path> include_paths; std::function<std::optional<std::string>(std::string_view name)> include_resolver; };`
  - `struct schema { cef_file file; std::string record_marker; std::string data_until; std::vector<std::vector<token>> nrv_data; };` (`nrv_data[i]` = `DATA` tokens of `file.variables[i]`, empty if record-varying) (`record_marker` default `"\n"`; `data_until` empty = EOF)
  - `schema parse_header(const lexed_header&, const input_buffer&, const load_options&)`

Rules:
- Structural keywords (case-insensitive): `FILE_FORMAT_VERSION`, `END_OF_RECORD_MARKER`, `DATA_UNTIL`, `INCLUDE`, `START_META`/`END_META`/`ENTRY`/`VALUE_TYPE`, `START_VARIABLE`/`END_VARIABLE`/`SIZES`/`VALUE_TYPE`/`DATA`. Every other top-level `KEY = v` → global attribute; inside a variable block → variable attribute. `FILE_NAME`, `FILE_FORMAT_VERSION` also kept as globals.
- `DATA_UNTIL = EOF` (any case, quoted or not) → empty.
- `START_META = X … END_META = X`: global attribute `X`, entries from `ENTRY` lines; a `VALUE_TYPE` line retypes every following `ENTRY` in that block (facts Q9). Mismatched `END_META` name → error.
- Scalar conversion: entries are kept as `std::string` (text preserved, e.g. `ENTRY = 090213` stays `"090213"`; CEFLIB CEF.c:1283-1289). Only under an explicit `VALUE_TYPE` in a meta block are they converted (INT/BYTE → `int64_t`, FLOAT/DOUBLE → `double`, ISO_TIME → `iso_time`, ISO_TIME_RANGE → `iso_time_range`, CHAR → string); failure → `parse_error`. Variable attributes stay strings, except `FILLVAL`, converted with the variable's own type (an ISO_TIME fill that doesn't parse, like `9999-99-99T99:99:99Z`, stays a string).
- `SIZES`: values are integers ≥ 1; a single quoted token like `"64, 6, 32"` is split on commas (corpus deviation). `SIZES = 1` → `sizes = {1}`.
- `DATA = …` present → `is_nrv = true`; tokens go to `schema::nrv_data` for Task 5.
- `FILLVAL` → kept as attribute (scalar conversion like any attribute) and its raw token text in `fillval_token`.
- `INCLUDE = name` (quoted or not, in top level or inside a meta block): resolve in order (1) `include_resolver(name)`, (2) `input.directory / name`, (3) each `include_paths` entry, (4) each dir of env `CEFPATH` (`:` separated, `;` on Windows), (5) `name` relative to CWD (CEFLIB compat, facts Q2). Included text is lexed and spliced in place; recursion depth > 16 → error (cycles). Not found → `parse_error(source, line, "INCLUDE file not found: <name>")`.
- Version check per Global Constraints, done after all includes.

- [ ] **Step 1: Write the failing tests:** `minimal_file` (one ISO_TIME + one FLOAT SIZES=3 variable; names, types, sizes, `record_marker == "\n"`); `version_missing_raises`; `version_cef1_raises`; `meta_block_entries` (MISSION_REGION → 11 string entries, from the corpus WHI header copied as fixture); `meta_value_type_iso_time_range` (MISSION_TIME_SPAN → one `iso_time_range`); `untyped_entries_keep_text` (`ENTRY = 090213` → `"090213"`, `DELTA_PLUS = 11.250` → `"11.250"`); `fillval_typed_by_variable` (INT var `FILLVAL = 99` → `int64_t 99`, FLOAT `-1e+31` → double, time `9999-99-99T99:99:99Z` → string); `sizes_quoted_with_spaces` → `{64, 6, 32}`; `nrv_data_detected` (theta with `DATA = -78.750, …` → `is_nrv`); `marker_dollar_unquoted` and `marker_semicolon_quoted`; `data_until_end_data` and `data_until_eof`; `include_from_directory`, `include_from_resolver` (memory input), `include_nested`, `include_missing_raises`, `include_cycle_raises`, `error_in_included_file_names_it` (bad line in `nested.ceh` → message starts with `nested.ceh:`); `end_meta_mismatch_raises`; `end_variable_mismatch_raises` (exact, case-sensitive name); `start_variable_inside_open_block_raises`.
- [ ] **Step 2: Run** `meson test -C build header` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `meson test -C build` → all pass.
- [ ] **Step 5: Commit** — `feat: header parser, attributes and INCLUDE resolution`.

### Task 5: Typed value parsing + non-record-varying data

**Files:**
- Create: `include/cefast/values.hpp`, `tests/cpp/test_values.cpp`
- Modify: `include/cefast/header/parser.hpp` (fill NRV columns from `nrv_data` at the end of `parse_header`)

**Interfaces:**
- Produces: `bool append_value(column& col, value_type type, std::string_view token, std::string_view fillval_token)` — returns `false` on an invalid token; the caller (header or record parser) raises `parse_error` with its own source/line/record. Conversions: FLOAT `fast_float::from_chars<float>`, DOUBLE `<double>` (full-token match), INT `std::from_chars<int32_t>` (leading `+` and leading zeros accepted), BYTE integer 0–255, ISO_TIME/RANGE per Task 1 with the FILLVAL→NaT rule, CHAR → token text (quoted or not; unquoted is an accepted deviation).
- NRV: `records = 1`; shape per Global Constraints.

- [ ] **Step 1: Write the failing tests:** `float_values` (`-1.00000e+31` → `-1e31f`), `double_full_precision`, `int_leading_zero` (`05` → 5), `int_garbage_raises` (`5x`), `byte_out_of_range_raises` (`256`), `time_fill_is_nat` (var FILLVAL `9999-99-99T99:99:99Z`, token same → `nat`), `time_invalid_not_fill_raises`, `range_value`, `char_quoted_keeps_spaces`, `nrv_theta_values` (8 floats from the CODIF fixture, exact values `-78.75f … 78.75f`), `nrv_count_mismatch_raises` (SIZES=8, 7 values), `nrv_data_continuation` (`DATA = 1, 2, \` + `3` → 3 values), `byte_values` (`0`,`1`,`127`,`255`), `int_out_of_int32_raises` (`2147483648`).
- [ ] **Step 2: Run** → FAIL. **Step 3: Implement.** **Step 4: Run** `meson test -C build` → all pass.
- [ ] **Step 5: Commit** — `feat: typed value parsing and non-record-varying data`.

### Task 6: Data record parser

**Files:**
- Create: `include/cefast/data/records.hpp`, `tests/cpp/test_records.cpp`, `tests/resources/fixtures/records/*.cef` (hand-made)

**Interfaces:**
- Consumes: `schema`, `append_value`.
- Produces: `void parse_records(schema&, std::string_view data, std::size_t first_line, std::string_view source)` — fills record-varying columns, sets `records` on each record-varying variable.

Rules (facts Q4–Q8):
- Data ends at the first line whose left-trimmed text starts with `data_until` (spec: "found at the beginning of a line"; CEFLIB prefix-matches), if set; else at end of input.
- Expected tokens per record = Σ over record-varying variables (declaration order) of Π `sizes`; tokens go to variables in that order, C order within a variable (last index fastest, facts Q1).
- Marker `"\n"`: each non-blank, non-comment line is one record. Other marker: newlines are whitespace; a record ends at the marker (which is matched outside quotes, after trimming whitespace).
- `!` outside quotes → skip to end of line. Quoted tokens verbatim.
- Field count ≠ expected → `parse_error(source, line, "expected N values, got M", record)`. One trailing comma right before the marker is tolerated (accepted deviation); an empty field elsewhere is an error.
- Pre-allocation: if a `!RECORDS= N` comment is found in the last 256 bytes before the end, `reserve(N × per-record)`; never trusted for the count.

- [ ] **Step 1: Write the failing tests:** `newline_marker_basic` (3 records, values checked); `dollar_marker_trailing_space` (`…,3 $`); `multi_line_records_with_comment` (`;` marker, a record split over 3 lines, a `!` line between records); `quoted_char_with_separators` (`"a,b!c"` → `a,b!c`); `crlf`; `trailing_comma_tolerated`; `empty_field_raises`; `short_record_raises` (message has record index and line); `long_record_raises`; `multidim_c_order` (SIZES=2,3, tokens `0..5` → values `0..5` in order, shape `{records,2,3}`); `empty_and_single_record`; `records_hint_is_only_a_hint` (hint 10 for 3 records, hint 1 for 3 records → both give 3); `data_until_stops` (lines after `END_DATA` ignored; also `END_DATA  ! end`).
- [ ] **Step 2: Run** → FAIL. **Step 3: Implement** a single-pass scanner. **Step 4: Run** `meson test -C build` → all pass.
- [ ] **Step 5: Commit** — `feat: scalar data record parser`.

### Task 7: Public `load()` + corpus test + deviations table

**Files:**
- Create: `include/cefast/cefast.hpp`, `tests/cpp/test_corpus.cpp`, `tests/resources/csa/*.cef` (copied per Global Constraints), `docs/accepted-deviations.md`
- Test data generation: none (real files).

**Interfaces:**
- Produces: `cef_file load(const std::filesystem::path&, const load_options& = {})`, `cef_file load(std::span<const char>, const load_options& = {})`.

- [ ] **Step 1: Write the failing tests:** `every_corpus_file_loads` (loop over `tests/resources/csa/*.cef`; `REQUIRE_NOTHROW`; each record-varying variable has `records` equal to the `!RECORDS=` count of that file); spot checks on known values read from the files with `grep` while writing the test: WHI last record (`2002-02-02T11:57:13.320Z`, `0.743000`, `"A"`, `"20"`, …, `3`), CODIF `3d_ions` shape `{records, 8, 16, 31}` and first value, EFW L3 microsecond times; `gz_equals_plain` (compress a corpus file with `cefast::gzip`, load both, compare all columns); `range_variable` (GRMB file: an ISO_TIME_RANGE variable has shape `(records, 2)` when loaded, values match the file text).
- [ ] **Step 2: Run** → FAIL. **Step 3: Implement** `load` = `read_input` → `lex_header` → `parse_header` → `parse_records`.
- [ ] **Step 4: Run** `meson test -C build` → all pass. Fix parser bugs found by the corpus with a new unit test in Tasks 3–6's test files first (reproducer-first).
- [ ] **Step 5: Write `docs/accepted-deviations.md`**: one row per deviation (unquoted INCLUDE/marker/ENTRY, ISO_TIME_RANGE in globals, quoted SIZES with spaces, continuation outside variable blocks, trailing comma, unquoted CHAR, `:60` seconds, >9 fraction digits), each naming its test.
- [ ] **Step 6: Commit** — `feat: public load() and CSA corpus tests`.

### Task 8: Python module

**Files:**
- Create: `pycefast/_cefast.cpp`, `pycefast/meson.build`, `cefast/__init__.py`, `pyproject.toml` (meson-python, name `cefast`, requires-python `>=3.10`, deps `numpy`), `tests/python/test_cefast.py`
- Modify: `meson.build`, `tests/meson.build` (`test('python', python3, args: [...], env: ['PYTHONPATH=' + meson.project_build_root()])`)

**Interfaces:**
- Consumes: `load`, model types, `cpp_utils::python::{view_with_owner, as_datetime64_ns, to_datetime}`.
- Produces (Python):
  - `cefast.load(source: str | os.PathLike | bytes | bytearray | memoryview, *, include_paths=(), include_resolver=None) -> CEF` — parse runs with the GIL released; `include_resolver` is called with the GIL re-acquired.
  - `CEF`: `__getitem__(name) -> Variable` (KeyError), `__contains__`, `__iter__` (names, file order), `__len__`, `keys()`, `attributes: dict[str, value]`.
  - `Variable`: `name`, `value_type` (`"FLOAT"`…), `sizes` (tuple), `is_nrv`, `attributes: dict`, `values: np.ndarray`, `__repr__`.
  - Attribute values: one entry → the scalar, several → list; `iso_time` → `datetime.datetime` (naive UTC), `iso_time_range` → tuple of two datetimes.
  - `values`: numeric and time arrays are zero-copy views (owner = `shared_ptr<cef_file>`); dtypes and shapes per Global Constraints; CHAR copied.
  - `cefast.ParseError(ValueError)`.

- [ ] **Step 1: Write the failing tests** (unittest): `test_load_path_and_bytes_equal`, `test_load_gz_bytes`, `test_time_dtype` (WHI `time_tags…` is `datetime64[ns]`, last == `np.datetime64('2002-02-02T11:57:13.320')`), `test_multidim_shape` (CODIF `(n, 8, 16, 31)`), `test_nrv_shape` (theta `(8,)`, `is_nrv`), `test_char_values` (WHI Spectrum_Type last == `'A'`), `test_attributes_plain_values` (`f.attributes['MISSION']` == `'Cluster'`, `MISSION_REGION` is a list of 11 str, `MISSION_TIME_SPAN` is a tuple of 2 datetimes), `test_values_outlive_file` (`v = cefast.load(p)['…'].values; gc.collect(); v.sum()` works), `test_parse_error_type` (corrupt bytes → `cefast.ParseError` and `ValueError`), `test_include_resolver` (fixture from Task 4 loaded from bytes with a dict-backed resolver), `test_missing_key` → `KeyError`, `test_fill_time_is_nat`, `test_dtypes` (one variable per type in the corpus: float32, float64, int32, `<U`, datetime64[ns]), `test_nrv_scalar_shape` (`SIZES = 1` NRV → `(1,)`), `test_varying_scalar_shape` (FGM `time_tags…` → `(n,)`).
- [ ] **Step 2: Run** `meson test -C build python` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `meson test -C build` → all pass. Also `pip install -e . --no-build-isolation` in `~/.cache/cdfpp-venv`-style venv and `python -c "import cefast"` from another directory.
- [ ] **Step 5: Commit** — `feat: cefast Python module`.

### Task 9: CEFLIB differential check

**Files:**
- Create: `tools/ceflib/build_ceflib.sh` (downloads CEFLIB-1.8.x tarball from ceflib.irap.omp.eu into `~/.cache/cefast-ceflib`, builds the Python module there), `tests/python/test_vs_ceflib.py` (skipped when `ceflib` not importable; each corpus file in its own subprocess — CEFLIB is a global singleton)

**Interfaces:**
- Consumes: `cefast.load`.

Normalisation (design "Testing"): CEFLIB strips `__<DATASET_ID>` from variable names **and** from every variable attribute value (CEF.c:431, :1345) → strip both on our side; its `gattrs()` only has FILE_NAME/FILE_FORMAT_VERSION/END_OF_RECORD_MARKER/DATA_UNTIL → compare that subset; variable attributes compared only for SIZES/VALUE_TYPE/FILLVAL/DEPEND_i; floats `rtol=1e-6` for FLOAT, `1e-12` for DOUBLE; times: CEFLIB ms since 1958 → convert, compare to 1 ms; BYTE skipped; CHAR compared stripped.

- [ ] **Step 1:** write `test_vs_ceflib.py` and run it once with CEFLIB built → record mismatches.
- [ ] **Step 2:** every mismatch is either a cefast bug (reproducer test in Tasks 3–6 files, fix) or a CEFLIB quirk (add to `docs/accepted-deviations.md` "CEFLIB differences" section with evidence).
- [ ] **Step 3: Run** `meson test -C build` → all pass.
- [ ] **Step 4: Commit** — `test: differential check against CEFLIB`.
