# CEFast Speed Plan (round 5): speculative parallel split

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make the parallel path actually parallel. Today a serial byte-by-byte pre-scan (`split_records`) finds the
chunk boundaries before any thread starts. On PEA (0.83 MB) that pre-scan costs ~2.2 ms of a 2.6 ms load, so the
parallel load is *slower* than the serial one (0.52 s vs 0.47 s for 200 loads) and does 65 % more instructions.

**Architecture:** cut speculatively, parse in parallel, verify, fall back.

1. Cut points: for `k = 1..T-1`, start at `k * size / T`, find the next occurrence of the record marker
   (`body.find(marker, offset)`), cut right after it. Drop duplicate/empty chunks. No marker found → fewer chunks.
2. Parse every chunk concurrently with its own `record_scanner`, each assuming it starts in the clean state
   (not quoted, not in a comment, at the start of a record).
3. Verify: every chunk except the last must have **ended on a real marker at its last byte** (the scanner's
   marker branch consumed the bytes up to `data.size()`). Chunk 0 starts clean for real; if chunk k ended on a
   real marker, chunk k+1 really started clean — so the whole result is exact by induction. A cut that fell
   inside a quote makes that chunk throw ("unterminated quote") or end not-on-a-marker; a cut inside a comment
   ends inside the comment → not on a marker.
4. Any chunk error or failed verification → discard the parts and run the serial `record_scanner` on the whole
   body. Error messages (line, record number) therefore come only from the serial scanner and are identical to
   the serial path by construction; per-chunk line/record bookkeeping is no longer needed.
5. Concatenate columns in file order as today; `records` = sum of chunk records.

**Tech Stack:** unchanged.

## Global Constraints

- No existing test edited or deleted. `meson test -C build` 9/9, `meson test -C build-asan` 7/7.
- Every commit body ends with: `perf stat` line for PEA, CODIF H1, CODIF He2 and WHI, serial (`threads=1`) and
  default threads: wall ms/load and instructions/load, from `build-release/tools/bench/bench_load <file> 200 <t>`;
  plus the `tools/ceflib/benchmark.py --runs 7` total and per-file median line.
- Counters are mandatory (user rule): instructions, cycles, branch-misses, page-faults for each claim.
- One build/test/benchmark at a time, foreground only. Never two against one build dir.
- Use the venv meson: `export PATH=~/.cache/cdfpp-venv/bin:$PATH` (build dirs were set up with meson 1.12).
- Never push.

## Review Focus

1. Marker inside a quoted CHAR exactly at a cut point (quotes may span lines) → parallel falls back, result equal to serial.
2. Marker inside a `!` comment at a cut point → falls back, equal to serial.
3. Error in chunk 3 of 4 → message identical to serial (record number and line), never a chunk-relative one.
4. Multi-character marker → same algorithm via `find(marker)`; existing multi-char tests keep passing.
5. Last record without a trailing marker (end of file) → counted once (fix `7f75176` must keep holding).

---

### Task A: Speculative split

**Files:** `include/cefast/data/parallel.hpp`, `include/cefast/data/records.hpp` (scanner exposes whether it ended on
a marker), `tests/cpp/test_corpus.cpp` (additions only), `tools/fuzz/fuzz_load.cpp` only if an interface changes.

**Interfaces:**
- `record_scanner` gains `[[nodiscard]] bool ended_on_marker() const noexcept` — true iff the last bytes of `data`
  were consumed by the marker branch of `run()`.
- `split_records` is replaced by `std::vector<std::size_t> speculative_cuts(std::string_view body,
  std::string_view marker, std::size_t parts)` (cut offsets, strictly increasing, each right after a marker).
- Chunk scanners get a size hint: the `!RECORDS` hint of the whole body scaled by `chunk_bytes / body_bytes`
  (rounded up), or no hint when the body has none.
- `parallel_threshold` stays a test hook (fuzz forces the parallel path).

- [ ] Tests first (each forces the parallel path with `parallel_threshold = 0` and `threads = 4`, and compares
  every column + `records` with the serial load): `parallel: cut inside a quoted value falls back`,
  `parallel: cut inside a comment falls back`, `parallel: error in a late chunk has the serial message`
  (compare `what()` strings), `parallel: multi-character marker`. Build inputs so a marker sits at each of the
  4 cut offsets (size/4, size/2, 3·size/4) inside a quote / a comment. Verify red where expected on the old
  code (some may already be green: say which).
- [ ] Implement. Remove `split_records`, the per-chunk `first_line`/`first_record` plumbing if unused.
- [ ] Full suite, ASan suite, replay fuzz corpora (`build-fuzz/tools/fuzz/fuzz_load ~/.cache/cefast-fuzz/corpus/load
  -runs=0 -rss_limit_mb=2048`) then 10 min of `fuzz_load` (`-max_len=262144 -dict=tools/fuzz/cef.dict`).
- [ ] Counters + benchmark; commit `perf(parallel): speculative split, verify, fall back to serial`.

### Task B: Re-tune the threshold and the per-chunk overhead

- [ ] With Task A in, measure serial vs default threads for corpus files between 100 KB and 2 MB (and a few
  synthetic sizes made by truncating CODIF at record boundaries: 64, 128, 256, 512 KiB). Set
  `parallel_threshold` where parallel starts winning on wall time; write the table in
  `docs/perf/2026-11-round5-parallel.md`.
- [ ] If `perf record` still shows `schema` copies (`std::vector<schema> parts(..., result)`) or the column
  concatenation above 5 % of task-clock, reduce them (e.g. scanners on variable copies only, or reserving the
  destination once from the summed chunk sizes). Counters before/after. Revert anything inside ±3 %.
- [ ] Commit per change.

Report to the file named in the dispatch prompt: commits, counters table before/after, fuzz results, anything reverted.
