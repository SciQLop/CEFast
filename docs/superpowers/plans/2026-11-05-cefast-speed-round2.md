# CEFast Speed Plan (round 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Beat CEFLIB on every corpus file (today: total 0.95×, per-file median 0.84×, CODIF 1.16×), cheapest measured wins first.

**Architecture:** No pipeline change. Each task replaces one hot spot named in `docs/perf/2026-11-round1-profile.md`, gated by a fresh profile.

**Tech Stack:** as round 1; xsimd (wrapdb) only if Task C chooses SIMD.

**Spec:** design "Performance approach"; ranking: `docs/perf/2026-11-round1-profile.md` "Round 2".

## Global Constraints

- All round-1 constraints hold: no existing test edited or deleted; every commit ends with the `tools/ceflib/benchmark.py` line from `build-release` (median of 5, idle machine); a change inside ±3 % noise is reverted and reported.
- Every SIMD or otherwise "clever" path keeps a scalar twin, selected at build or run time, and a test runs the whole corpus through both and compares every column (design anti-goal).
- Small files matter as much as big ones: speasy loads many small files one by one. Report per-file median as well as the total.
- Never push. The repo now has `origin` (github.com/SciQLop/CEFast); history was rewritten before the first push, so earlier commit hashes in reports are stale.

## Review Focus

1. Header-only files and files with one record (corpus has 6 + 1): fastest path must not break the "empty data" logic — corpus test covers them.
2. Comments (`!`) and quotes spanning a SIMD block boundary (64 bytes) — Task C test `structural_scan_block_boundaries` builds records where `"`, `!`, `,` and the marker sit at offsets 62, 63, 64, 65.
3. CHAR values longer than the first record's width (fixed-width buffer must grow or size from the longest) — Task D test `char_width_grows`.
4. Multi-character markers keep the generic path — existing test `a multi-character marker still ends records`.
5. INCLUDE errors still name the included file after the header rewrite — existing test `error_in_included_file_names_it`.

---

### Task A: Re-profile after the marker fast path

**Files:** Modify `docs/perf/2026-11-round1-profile.md` (append a "After marker fast path" section).

- [ ] Run `bench_load` + `perf record -g` on CODIF, WHI and the smallest header-only file; report % in `run()`, header lexing/parsing, `append_value`, CHAR construction, `parse_iso_time`.
- [ ] Decide and write down the order of Tasks B/C/D by measured share (largest first). Skip any task whose hot spot is < 5 % on every profiled file, and say so.
- [ ] Commit `docs(perf): profile after the marker fast path`.

### Task B: Header parsing without per-line vectors

**Files:** Modify `include/cefast/header/lexer.hpp`, `include/cefast/header/parser.hpp`; tests additions only.

**Interfaces:** `lex_header` may become a callback/visitor or produce `std::string_view`-based lines into the input text (input outlives parsing; included files keep their text alive in an arena owned by the parse). `header_line` keeps a `source` for errors. Quoted continuation lines (`\`) are the only case that needs owned text: join into the arena.

- [ ] Test first: `header_only_file_allocations` — count allocations with a replacement `operator new` in the test TU while lexing+parsing the WHI header fixture; assert below a bound set from the new code (e.g. < 1 per header line) and red on the old code.
- [ ] Implement; full suite; benchmark (report the small-file median).
- [ ] Commit `perf(header): lex into views, no per-line allocations`.

### Task C: Structural scan of the record body

**Files:** Create `include/cefast/data/structural.hpp`; modify `records.hpp`; tests additions only.

**Interfaces:** `structural.hpp` produces, per 64-byte block, bitmasks for `,`, marker (single char), `"`, `!`, `\n`; quote and comment regions masked out (prefix-XOR over quote bits; comment = from `!` to next `\n` outside quotes). `record_scanner` walks set bits instead of characters. Scalar twin: today's character loop, kept and selectable (`CEFAST_SCALAR_ONLY` or a runtime flag in tests).

- [ ] Tests first: `structural_scan_block_boundaries` (see Review Focus 2), `structural_matches_scalar_on_corpus` (every corpus file, every column equal between the two paths).
- [ ] Start with the portable version (`memchr`-driven or SWAR 8-byte words); add xsimd only if the portable version leaves `run()` > 25 % and the profile says the scan is the cost.
- [ ] Commit per step with benchmark lines.

### Task D: CHAR storage as one fixed-width buffer

**Files:** Modify `include/cefast/model.hpp` (column alternative for CHAR: `struct char_column { std::size_t width; no_init_vector<char> data; }` padded with `\0`), `values.hpp`, `records.hpp`, `pycefast/_cefast.cpp` (numpy `S{width}` view decoded to `U` — or keep `U` copy, measure), tests additions only.

- [ ] Tests first: `char_width_grows` (first record `"A"`, later `"ABCD"` → all values intact), `char_trailing_spaces_kept` (quoted `"a  "` stays `a  `; padding is `\0`, not space).
- [ ] Implement; full suite incl. Python CHAR tests; benchmark (WHI is the target file).
- [ ] Commit `perf(char): fixed-width buffer per CHAR variable`.

### Out of this round

SIMD ISO time (≤ 5 % measured) and parallel chunks (complexity; only for multi-MB files) wait for a user need or a round-3 profile.
