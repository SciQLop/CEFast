# CEFast Speed Plan (round 7): persistent workers, parse-in-place, SIMD integer runs

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make small and mid-size files faster. Measured on a quiet machine (Ryzen 7 5800X, 2026-11-06):

- Starting and joining 7 `std::thread`s costs ~100 µs (median 108, min 89). A whole C1_PP_DWP load (130 KB) is
  ~330 µs. That start-up is most of the parallel overhead that put break-even at a ~190 KiB body.
- `parse_records_parallel` copies the whole `schema` per chunk (`std::vector<schema> parts(chunks, result)`),
  attributes included: on DWP ~1250 allocations per copy.
- Serial DWP profile: `record_scanner::run` 28 %, `add` 12 %, `parse_float` 6 %, header ~15 %, malloc/free ~8 %.
  IPC 3.6-3.7, L1d miss 1.5 %: instruction-bound, not memory-bound. Each data byte is read twice: once by
  `next_stop` to find the token end, again by the number parser.

**Architecture:** three independent tasks, in this order (A gives the most, C is a spike).

- A: a process-wide pool of persistent workers replaces per-load `std::thread`s; chunks get only what the scanner
  needs, not a full `schema` copy; re-measure and lower `parallel_threshold`.
- B: for numeric slots, the number parser finds its own end (as simdjson does); the byte after it must be a stop
  byte, else fall back to today's path, so behaviour and error messages are unchanged by construction.
- C: Wojciech Muła's SIMD parsing of integer sequences (http://0x80.pl/articles/simd-parsing-int-sequences.html)
  for runs of small integers, behind runtime CPU dispatch. Kept only if it pays.

## Global Constraints

- No existing test edited or deleted. `meson test -C build` 9/9, `meson test -C build-asan` 7/7 (+ new tests).
- Counters mandatory for every commit (user rule): instructions, cycles, branch-misses, page-faults, task-clock per
  load via `perf stat build-release/tools/bench/bench_load <file> 2000 <threads>` on C1_PP_DWP, C4_CP_AUX_ESOC_MEP,
  C1_CP_EDI_MP, PEA and CODIF H1, serial (`1`) and default (`0`); plus `tools/ceflib/benchmark.py --runs 21` (run
  with `python3 -u`, from outside the repo root, `PYTHONPATH=<repo>/build-release`) total / median / min line.
  Revert anything inside ±3 %, and say so in the report.
- `export PATH=~/.cache/cdfpp-venv/bin:$PATH`. One build/test/benchmark/fuzz at a time, in the foreground, never
  backgrounded. Never push. /tmp is RAM: scratch in `~/.cache/cefast-scratch`, removed at the end.
- Header-only library stays header-only.
- Fuzz corpora in `~/.cache/cefast-fuzz/corpus/*` are grown data: never delete or recreate them.

## Review Focus

1. A load that returns (or throws) while a pool worker has not yet picked up its job must not leave that job
   touching freed stack state.
2. Two Python threads loading two files at the same time (GIL released) share the pool: both finish, results equal
   the serial loads.
3. `os.fork()` after a parallel load, then a parallel load in the child: must not deadlock or crash.
4. Parse-in-place on `12abc`, `1.5e`, `1"x"`, `-`, `+.`, `inf`, `nan`, `1,,2`, a number touching a `!` comment or the
   marker, CRLF, a multi-character marker: same values or same `what()` as today.
5. Fill-value columns (`-1.00000e+31`) keep matching the FILLVAL text comparison after parse-in-place.

---

### Task A: Persistent worker pool, lighter chunks, new threshold

**Files:** `include/cefast/data/parallel.hpp` (pool may go in a new `include/cefast/data/workers.hpp` if
`parallel.hpp` grows past ~250 lines), `tests/cpp/test_corpus.cpp` (additions only), a Python test for
fork/concurrency under `tests/python/` (additions only).

**Interfaces:**
- `detail::workers()` returns a process-wide pool, created on first use, with `default_threads() - 1` workers.
  `void run_on_workers(std::size_t jobs, F&& job)`: runs `job` on up to `jobs` workers *and* on the calling thread,
  and returns when every chunk is done. The caller keeps taking chunks from the shared atomic counter (as `work`
  does today). So a busy or dead pool degrades to the caller doing all chunks: never a deadlock.
- The state a job touches (counter, parts, errors) lives in a `std::shared_ptr` captured by the job. A late worker
  that wakes after the load returned only finds the counter exhausted (Review Focus 1).
- Fork safety: the pool records `getpid()` at creation. A load in a process whose pid differs builds a fresh pool
  and leaks the old one on purpose (its threads do not exist in the child; destroying its mutex is undefined).
  `simplify:` comment naming that leak. No `pthread_atfork`.
- Pool threads are never joined at exit: detach them, or keep the pool in a leaked `new`, so static destruction
  order and Python interpreter shutdown cannot hang.
- WebAssembly / no-thread builds: keep today's guard. The serial path is unchanged.
- Chunks no longer copy the `schema`: the chunk scanner gets copies of the variables' column descriptions only
  (type, sizes, fill, is_nrv), not `globals` and not the per-variable `attrs`. If `record_scanner` needs a `schema&`
  for that, give it a lean one (an empty `schema` holding variables with empty `attrs`) built once per load, not
  once per chunk from `result`.

- [ ] Tests first (each pass today, so they pin behaviour): `parallel: two concurrent loads share the workers`
  (C++, two `std::thread`s, each forcing `parallel_threshold = 0`, `threads = 4`, 50 loads each, equal to serial);
  Python `test_fork_after_parallel_load` (Linux only: parallel load, `os.fork()`, child does a parallel load and
  `os._exit(0 if equal else 1)`; parent asserts exit 0 within 30 s); `parallel: load returns before a slow worker`
  (threads = 64 on 4 chunks, many loads, under ASan).
- [ ] Implement the pool and the lean chunk state. Full suite, ASan suite, `fuzz_load` corpus replay
  (`-runs=0 -rss_limit_mb=2048`) then 10 min of `fuzz_load` (`-max_len=262144 -dict=tools/fuzz/cef.dict`).
- [ ] Counters; commit `perf(parallel): persistent workers, chunks without schema copies`.
- [ ] Re-measure the crossover as in round 5 (CODIF truncated at record boundaries: 16, 32, 64, 96, 128, 192,
  256 KiB body, plus DWP, C4, EDI_MP, MAARBLE), serial vs default threads, median of 3 × 200 loads. Set
  `parallel_threshold` ~35 % above break-even. Append the table to `docs/perf/2026-11-round5-parallel.md` under
  "Round 7: persistent workers". Commit `perf(parallel): threshold for persistent workers`.

### Task B: Parse numbers in place

**Files:** `include/cefast/data/records.hpp`, `include/cefast/values.hpp` (parsers that return the end pointer),
`tools/fuzz/reference_records.hpp` (verbatim frozen copy of today's `records.hpp` + whatever of `values.hpp` /
`structural.hpp` it needs, in namespace `cefast::reference`, not shipped), `tools/fuzz/fuzz_records_diff.cpp`,
`tools/fuzz/meson.build`, CI `fuzz` job loop, `tools/fuzz/make_corpus.py` (seed dir for the new target; mkdir only),
`tests/cpp/test_records.cpp` or the existing records test file (additions only).

**Interfaces:**
- `fuzz_records_diff`: the fuzzer input is split into a small fixed schema description (first bytes pick 1-6 slots
  among FLOAT/DOUBLE/INT/BYTE/ISO_TIME/CHAR, a FILLVAL or none, marker `$`, `$$` or `\n`) and a body. It runs the new
  and the reference scanner. Both throw with equal `what()`, or both give equal columns and record counts. Any
  mismatch → `__builtin_trap()`.
- Fast path, in `run()`: at value start, when the next byte is not a stop byte, the slot is FLOAT/DOUBLE/INT/BYTE,
  and the marker is a single character: parse with an end-pointer parser (`fast_float::from_chars` result `ptr`;
  `std::from_chars` for INT/BYTE, keeping the small-integer shortcut). Accept only if the parse succeeded, consumed
  at least one byte, and the byte at `ptr` is a stop byte for `next_stop` (`is_stop`, same set exactly) or end of
  data. Then the token is `[start, ptr)`: go through the same `store`/fill/range checks as today and continue the
  loop at `ptr` in state `after_value`. In every other case run today's code from the token start, unchanged.

- [ ] Task B1: frozen reference + `fuzz_records_diff`. Run 5 min against the unchanged scanner: must be clean.
  Commit `test(fuzz): differential target for the record scanner`.
- [ ] Task B2: tests for each Review Focus 4 and 5 input that is not covered yet (list which already were). They
  pass on today's code.
- [ ] Implement. Full suite, ASan suite, `fuzz_records_diff` 20 min, `fuzz_load` replay, then 10 min `fuzz_load`.
- [ ] Counters; commit `perf(records): numbers find their own end`.

### Task C (spike): SIMD integer runs with runtime dispatch

Only after A and B are in. Only for a run of consecutive INT/BYTE slots (DWP has 26 in a row).

- Dispatch the way the library can stay header-only: GCC/Clang `__attribute__((target("sse4.1")))` /
  `target("avx2")` function variants, chosen once with `__builtin_cpu_supports` into a function pointer.
  Scalar elsewhere (MSVC, WebAssembly, non-x86). This differs from CDFpp, which compiles per-ISA translation units in
  `src/arch/x86/`. Say in the report whether that header-only scheme compiled cleanly with both GCC and clang.
- Muła's method: load 16 bytes, build the separator mask, look up a shuffle pattern for the digit spans, pack the
  digits into lanes, convert with `pmaddubsw`/`pmaddwd`. Any byte outside `[0-9 ,]`, a sign, or a span longer than
  the table covers → stop the SIMD run and continue with the Task B path at that token.
- Must pass `fuzz_records_diff` 20 min and the suites. Keep it only if DWP improves ≥ 5 % in instructions **and**
  wall time (median of 3) with no other file worse than 3 %. Otherwise revert, and write the numbers in
  `docs/perf/2026-11-round7.md` either way.

Report to the file named in the dispatch prompt: commits, counters before/after per task, fuzz runs (execs, time,
findings), anything reverted, and the new threshold table.
