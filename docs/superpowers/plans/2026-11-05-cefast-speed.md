# CEFast Speed Plan (round 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the allocation and copy overheads of the scalar reader so CEFast beats CEFLIB on every corpus file, then profile to decide whether parallel or SIMD work is worth a round 2.

**Architecture:** Same pipeline, cheaper plumbing: the input layer maps files with `cpp_utils::io::memory_mapped_file` (no copy for plain `.cef`, in-memory input borrowed), and the record parser appends each value straight from the input text into its column through a precomputed slot table, with no per-token `std::string` and no per-record token buffer.

**Tech Stack:** as the reader plan; `cpp_utils::io::memory_mapped_file`; `perf` for profiling.

**Spec:** `docs/superpowers/specs/2026-11-05-cefast-design.md` ("Performance approach"). Baseline from Task 9 of `2026-11-05-cefast-reader.md`: 41 files, cefast 82 ms vs CEFLIB 34 ms (0.42×); CODIF 18.5 vs 6.9 ms.

## Global Constraints

- Behaviour is frozen: every existing test stays green, unchanged (`meson test -C build` → Ok 9, incl. `ceflib`). A task may add tests, never edit or delete one.
- Every commit message ends with the benchmark line from `tools/ceflib/benchmark.py`: `total: cefast X ms, CEFLIB Y ms, speedup Z×; CODIF: a ms vs b ms`. Same machine, median of 5, build type `release` (`meson setup --buildtype=release build-release`, benchmark against that dir; tests may run in `build`).
- A change that does not improve the benchmark by more than noise (±3 %) is reverted, not committed, and reported.
- One build/test/benchmark invocation at a time, in the foreground. Never two against the same build dir.
- CSA delivers plain `.cef`: plain-file speed is the target; `.gz` must not regress.

## Review Focus

1. Empty file (0 bytes): `memory_mapped_file` maps nothing — must raise `parse_error` ("DATA_UNTIL missing" from the lexer), not crash. Test in Task 1.
2. File that changes or is truncated while mapped: out of scope (documented `simplify:` comment naming it), but a file shorter than its header must still raise, not read past the mapping — covered by existing truncation tests through the mapped path in Task 1.
3. Python `bytes` / `bytearray` / `memoryview` input is now borrowed, not copied: the buffer must stay valid during the GIL-released parse (bytes immutable; bytearray export held). Test in Task 1: resize attempt on the bytearray during a resolver callback raises `BufferError` in Python, parse still correct.
4. Quoted empty value `""` and a record marker inside quotes still behave as today after the scanner rewrite — existing tests; add `quoted_empty_char` in Task 2 if not present.
5. A record longer than expected must raise as soon as the count is exceeded, without writing past the slot table — test `long_record_raises` exists; add `very_long_record_raises` (10× expected) in Task 2.

---

### Task 1: Memory-mapped and borrowed input

**Files:**
- Modify: `include/cefast/input.hpp`, `include/cefast/cefast.hpp`, `pycefast/_cefast.cpp` (bytes paths stop copying), `tests/cpp/test_input.cpp`, `tests/python/test_cefast.py`

**Interfaces:**
- Produces: `struct input_buffer { std::variant<std::monostate, cpp_utils::io::memory_mapped_file, std::string> storage; std::string_view text; std::string source; std::optional<std::filesystem::path> directory; };`
  - plain file → `memory_mapped_file`, `text` views the mapping;
  - gzip file → mapped, then `gunzip` into the `std::string` alternative, mapping dropped;
  - in-memory plain bytes → `monostate`, `text` borrows the caller's span (caller keeps it alive for the duration of `load`);
  - in-memory gzip → decompressed `std::string`.
- `input_buffer` is move-only (mapping).
- `read_input(path)` and `read_input(span)` keep their signatures. All consumers use `input.text` instead of `input.bytes`.
- Missing or unreadable file: `parse_error(path, 0, "cannot read file")` (same as today). Empty file: `text` empty, no mapping.

- [ ] **Step 1: Write the failing tests:** `mapped_equals_bytes` (load a corpus file by path and by span, compare all columns), `empty_file_raises_parse_error`, `borrowed_span_not_copied` (`read_input(span).text.data() == span.data()` for plain input). Python: `test_bytearray_locked_during_parse` (resolver tries `buf.extend(b'x')` → `BufferError`, load still succeeds).
- [ ] **Step 2: Run** `meson test -C build input python` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** full suite → Ok; benchmark in `build-release`.
- [ ] **Step 5: Commit** `perf(input): mmap files, borrow in-memory input` + benchmark line.

### Task 2: Allocation-free record parsing

**Files:**
- Modify: `include/cefast/data/records.hpp`, `include/cefast/values.hpp`, `tests/cpp/test_records.cpp` (additions only)

**Interfaces:**
- Consumes: `schema`, `variable`, `column`.
- Produces (internal to `records.hpp`):
  - `struct slot { column* values; value_type type; std::string_view fillval; };` and `std::vector<slot> slots` of size *expected tokens per record*, built once: slot *i* = the variable/type that token *i* of a record goes to.
  - The scanner keeps `std::size_t index` (next slot) per record; each token, quoted or not, is a `std::string_view` into the input text and is appended immediately via `append_value(*slots[index].values, …)`; `index == slots.size()` on another token → "expected N values, got more" immediately; `index != slots.size()` at the marker → "expected N values, got M".
  - No `std::vector<token>`, no `std::string text` buffer: unquoted tokens are `[begin, end)` ranges of the input.
- `append_value` keeps its signature (it already takes `string_view`); hoist the `std::visit` out of the per-value path if the profile shows it (typed appender per slot, e.g. a function pointer chosen at slot build time).
- CHAR still allocates its `std::string` (one per value is unavoidable with `std::vector<std::string>`; changing CHAR storage is round 2).

- [ ] **Step 1: Write the new tests first:** `very_long_record_raises` (10× expected values; message must contain `got more than 3` — the new code stops at the first extra value, the old code says `got 30`, so this is red), `quoted_empty_char` (`""` → empty string value, not an error; pins current behaviour, green before and after).
- [ ] **Step 2: Run** `meson test -C build records` → `very_long_record_raises` fails on the old code (red).
- [ ] **Step 3: Implement** the slot-table scanner.
- [ ] **Step 4: Run** full suite → Ok 9 (incl. `ceflib`). Benchmark.
- [ ] **Step 5: Commit** `perf(records): append values straight from the input` + benchmark line.

### Task 3: Gzip output sizing

**Files:**
- Modify: `include/cefast/gzip.hpp`, `tests/cpp/test_input.cpp` (additions only)

**Interfaces:**
- `gunzip` starts from the last member's ISIZE trailer (last 4 bytes, little-endian, = size mod 2³²) when it is ≥ the compressed size, else 4× the compressed size; ends with `shrink_to_fit()` only when capacity exceeds the size by more than 25 %.

- [ ] **Step 1: Test** `gunzip_capacity_close_to_size` (corpus file gzipped with `cefast::gzip`: `out.capacity() <= out.size() * 5 / 4`) → red on current code.
- [ ] **Step 2–4:** implement, full suite, benchmark (gz variant: add a `--gzip` flag to `tools/ceflib/benchmark.py` that gzips each corpus file in memory and times `cefast.load(bytes)`; CEFLIB column = n/a).
- [ ] **Step 5: Commit** `perf(gzip): size output from the ISIZE trailer` + both benchmark lines.

### Task 4: Profile and decide round 2

**Files:**
- Create: `docs/perf/2026-11-round1-profile.md`

- [ ] **Step 1:** `perf record -g` on a C++ driver loading CODIF and WHI 200× each (driver under `tools/bench/bench_load.cpp`, built only with `-Dwith_benchmarks=true`, committed).
- [ ] **Step 2:** Write the report: benchmark table after Tasks 1–3 (all 41 files, cefast vs CEFLIB), top 10 symbols with % for each file, MB/s per file, and a recommendation for round 2 ranked by expected gain: (a) parallel chunk parsing, (b) SIMD structural scan, (c) SIMD ISO time parse, (d) CHAR storage as fixed-width buffer, (e) header lexer allocations — each with the profile number that justifies it, or "not worth it" with the number that says so.
- [ ] **Step 3: Commit** `docs(perf): round 1 profile`.
