# CEFast Speed Plan (round 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every corpus file at or above CEFLIB's speed (today: total 1.04×, median 1.21×, min 0.87× on five dense numeric files), then make large files scale with cores.

**Architecture:** Part 1 removes per-value overhead in `append_value` (the 49 % self-time hot spot on CODIF) by writing through a pre-sized cursor and by short-circuiting the FILLVAL text. Part 2 splits the record body into whole-record chunks parsed by a thread pool, above a size threshold that the benchmark measures.

**Tech Stack:** as round 2; `cpp_utils/threading` from the wrap for Part 2 (same pool CDFpp uses), no new dependency.

**Spec:** design "Performance approach"; profile: `docs/perf/2026-11-round1-profile.md` ("After marker fast path" section).

## Global Constraints

- All round-1 and round-2 constraints hold: no existing test edited or deleted; every commit ends with the `tools/ceflib/benchmark.py` line from `build-release` (median of 5, idle machine); a change inside ±3 % noise is reverted and reported.
- Every fast path keeps a scalar twin plus a corpus-wide differential test comparing every column (design anti-goal).
- Small files matter as much as big ones: report the per-file median as well as the total.
- Never push.

## Review Focus

1. The `!RECORDS=` hint is only a hint: a file with more records than the hint must grow, and one with fewer must shrink to the real count (existing test `the records hint is only a hint`, plus new bounds tests).
2. Empty data sections and single-record files must stay correct after pre-sizing (corpus has 6 empty files).
3. Fill values whose text does not match `FILLVAL` must still take the parse path, including the `9999-99-99T99:99:99Z` time fill that becomes NaT.
4. Parallel parsing must produce byte-identical columns to serial, with errors reported from the first failing record in file order and the right line number.
5. Multi-character markers and files without a marker keep working (existing `a multi-character marker still ends records`).

---

### Task A: Write values through a pre-sized cursor, short-circuit FILLVAL

**Files:** Modify `include/cefast/data/records.hpp`, `include/cefast/values.hpp`; tests additions only.

- [ ] Test first: a column pre-sized from a hint smaller than the record count grows correctly, and a longer-than-hint file trims to the real count (bounds, empty and one-record cases); red on the current code is not expected — the tests guard the rewrite, and the *behaviour* is frozen by the existing corpus tests.
- [ ] Replace the per-value `push_back` with a `column_state` per variable: cursor and limit into the pre-sized buffer, `values.resize(hint × values_per_record)` up front for numeric types, doubling growth when the hint is short, and `resize(records × values_per_record)` at the end.
- [ ] Precompute the FILLVAL value once per variable; a token whose text equals it stores the constant without parsing.
- [ ] Full suite, benchmark, then commit `perf(records): write values through a pre-sized cursor`.

### Task B: Parallel chunk parsing

**Files:** Modify `include/cefast/data/records.hpp`, `include/cefast/cefast.hpp`, `include/cefast/model.hpp`, `pycefast/_cefast.cpp`; tests additions only.

- [ ] Measure the threshold: parse the synthetic large file (CODIF data concatenated ×20) serially at several sizes and pick the point where per-chunk overhead is paid back.
- [ ] `load_options::threads` (0 = auto, 1 = serial, n = n threads); Python kwarg `threads=`.
- [ ] One serial pass finds record boundaries outside quotes and comments, splits into N chunks of whole records, counts records per chunk, allocates the columns once, and parses chunks concurrently into disjoint slices with `cpp_utils::threading`.
- [ ] Error handling: the first error in file order wins, with its own line and record number (each chunk keeps its base line/record).
- [ ] Test first: `serial_equals_parallel` on every corpus file and on the synthetic ×20 file (all columns equal, records equal); `parallel_error_reports_first_record` (two bad records, the earlier one is reported); `threads_one_is_serial`.
- [ ] Full suite, benchmark, report the speedup vs threads on the largest corpus file and on the synthetic one; TSan run of `serial_equals_parallel` in a separate build dir.
- [ ] Commit `perf(records): parse record chunks in parallel`.
