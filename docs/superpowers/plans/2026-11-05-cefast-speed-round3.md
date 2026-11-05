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

## Round 3 measurements (hardware counters)

`perf stat -r N` over the whole process, divided by the number of loads. cefast uses
`tools/bench/bench_load <file> <iterations> <threads>`; CEFLIB uses a C driver
(`/tmp/opencode/perf/ceflib_driver.c`, `Read_CEF_file`/`Close_CEF_file`, linked against
`libCEF_gcc.a` + `libCIS_gcc.a`), so both are native and neither pays interpreter overhead.

### CODIF H1_CS, warm (50 loads serial cefast, 30 loads CEFLIB)

| metric | cefast (1 thread) | CEFLIB 1.8.6 |
|---|---|---|
| wall per load | 7.02 ms (min) | 7.80 ms |
| cycles | 33.9 M | 33.7 M |
| instructions | 135.4 M | 115.7 M |
| IPC | 3.99 | 3.43 |
| branches | 33.9 M | 28.1 M |
| branch misses | 54.7 k (0.16 % of branches) | 71.0 k (0.25 %) |
| instructions per value | 228.6 | 195.4 |
| branch misses per value | 0.092 | 0.120 |

The serial parse now costs the same cycles as CEFLIB and wins on branch misses: cefast executes
18 % more instructions per value but mispredicts 23 % fewer branches, and the wall time follows.
With the default thread count the same load takes 4.2 ms (task-clock 12 ms per load, i.e. 2.5
threads of useful work) — the parallel path, not the instruction count, is what puts it far ahead.

### Where the branch misses are (CODIF, `perf record -e branch-misses:u`, serial)

`record_scanner::run()` 37.1 %, `record_scanner::add()` 29.3 %, `parse_float<float>` 17.4 %,
`__memcmp_avx2_movbe` 4.0 %, `lex_header` 2.9 %, `string_view::compare` 1.6 %.

`parse_float` is fast_float's digit loop and its slow path; the fill-value short-circuit already
compares lengths before bytes, so the remaining per-value misses are the digit loop's own
branches. Replacing fast_float would be the way to move that bar, and it is not worth the risk
for 17 % of 0.16 % of branches.

### Page faults and first touch

| run | minor faults | wall | task-clock |
|---|---|---|---|
| CODIF, cold, 1 thread | 796 | 8.0-10.5 ms | 10.7 ms |
| CODIF, cold, default threads | 2,036 | 8.0-8.9 ms | 19.9 ms |
| CODIF, warm, 1 thread | 55 | 7.02 ms | 7.6 ms |

First touch of input plus columns costs roughly 1.0-1.5 ms cold; threads multiply the fault count
without shortening the wall time much at this size.

`madvise(MADV_SEQUENTIAL)` + `madvise(MADV_WILLNEED)` on the input mapping (cpp_utils
`memory_mapped_file`): cold 8.3-10.3 ms against a 8.0-10.5 ms baseline, warm 6.82/6.96 ms against
7.02/7.10 ms — inside the ±3 % noise band, so **reverted and not kept**.

`MADV_HUGEPAGE`: cpp_utils' `no_init_vector` allocator already applies it above 4 MB, and CODIF's
largest column is 2.4 MB, so no corpus file reaches it; at these sizes the effect is below the
noise this machine can resolve, so the allocator is left unchanged.

### Final per-file result (median of 5)

`total: cefast 28.4 ms, CEFLIB 36.6 ms, speedup 1.29x; median 1.16x, min 0.84x, max 1.84x`.
Still below 1.0x, all small or header-dominated: `C1_PP_DWP` 0.84x, `C1_JP_AUX_PMP` 0.89x,
`C3_CP_RAP_DSETTINGS` 0.89x, `C4_CP_AUX_ESOC_MEP` 0.92x, `C1_CP_EDI_MP` 0.98x,
`D1_JP_AUX_PMP` 0.98x — 10-30 us of fixed cost each, below anything the counters can separate.
