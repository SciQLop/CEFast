# CEFast Speed Plan (round 6): one-pass header lexer

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make small files (where CEFLIB still wins: C1_PP_DWP 0.90×, C4_CP_AUX_ESOC_MEP 0.92×, C1_CP_EDI_MP 0.99×)
faster by cutting header cost. On C1_PP_DWP (130 KB) the header is 70 KB of 130 KB: 932 `KEY = value` lines, 624 of
them `ENTRY = "<80-100 chars>"`. Profile (2000 serial loads): `lex_header` 16.6 %, `split_values` 12.5 %, malloc/free
~6 %, `shared_ptr` release 1.3 %. Each line is walked byte by byte three times (`scan_line`, `find_equals`,
`split_values`), each toggling a quote flag.

**Architecture:** one pass per line that jumps over quoted text with `memchr`/`find('"')` and over unquoted text to the
next byte of interest (`"`, `,`, `!`, `=` as appropriate). Same outputs (`lexed_header`, `header_line`, `token`), same
errors with the same messages and line numbers. The old lexer is kept, frozen, as a fuzz reference only
(`tools/fuzz/reference_lexer.hpp`, a verbatim copy of today's `lexer.hpp` in a `cefast::reference` namespace — not
shipped, not included by the library).

## Global Constraints

- No existing test edited or deleted. `meson test -C build` 9/9, `meson test -C build-asan` 7/7.
- Behaviour frozen: for every input, the new `lex_header` returns the same lines (key, tokens text+quoted, line
  number, data_offset, data_line) as the reference, or throws `parse_error` with the same `what()`.
- Counters mandatory for every commit (user rule): instructions, cycles, branch-misses, page-faults per load via
  `perf stat build-release/tools/bench/bench_load <file> 2000 1` on C1_PP_DWP, C4_CP_AUX_ESOC_MEP, C1_CP_EDI_MP and
  PEA; plus the `tools/ceflib/benchmark.py --runs 7` total / median / min line. Revert anything inside ±3 %.
- `export PATH=~/.cache/cdfpp-venv/bin:$PATH`. One build/test/benchmark/fuzz at a time, foreground. Never push.
- Machine is shared: medians of 3 for wall; instructions are the primary signal.

## Review Focus

1. `!` inside a quoted value (`ENTRY = "a ! b"`) is text, not a comment.
2. Quote characters inside an unquoted value (`a"b,c"d`): today the comma between them does not split — keep it.
3. Continuation lines (`\`) whose quoted value spans the join; a quote left open at end of line → same error.
4. `=` inside a quoted key part or value (`KEY = "a=b"`), and a line with no `=` → "expected KEY = value".
5. CRLF line endings and trailing spaces before `\`.

---

### Task A: Differential fuzz target first

**Files:** `tools/fuzz/reference_lexer.hpp` (copy of current `include/cefast/header/lexer.hpp`, namespace
`cefast::reference`), `tools/fuzz/fuzz_lexer_diff.cpp`, `tools/fuzz/meson.build`, CI `fuzz` job loop (add `lexer_diff`
with `max_len=65536`), `tools/fuzz/make_corpus.py` (seeds for the new target = the lexer seeds).

- [ ] `fuzz_lexer_diff`: run `cefast::lex_header(text, "f", flag)` and `cefast::reference::lex_header(...)` for
  `flag` in {true, false}; both throw with equal `what()`, or both return equal results (compare every field listed
  in Global Constraints). Any mismatch → `__builtin_trap()`.
- [ ] Build, run 2 min against the unchanged lexer: must be clean (it compares the code with itself).
- [ ] Commit `test(fuzz): differential target for the header lexer`.

### Task B: One-pass line lexer

**Files:** `include/cefast/header/lexer.hpp`; tests additions in `tests/cpp/test_lexer.cpp` (one per Review Focus line
not already covered — check first, list which were already there).

- [ ] Tests first for Review Focus items not covered yet; they must pass on the current lexer (they pin behaviour).
- [ ] Rewrite the per-line work as one pass. Also: stop copying the `shared_ptr` source per line if `perf` shows it
  (each `header_line` copy is an atomic inc/dec) — only if the interface change stays inside `lexer.hpp`/`parser.hpp`.
- [ ] Full suite, ASan suite; `fuzz_lexer_diff` 20 min, `fuzz_lexer` 5 min, `fuzz_load` corpus replay (`-runs=0`).
- [ ] Counters + benchmark; commit `perf(header): lex each line in one pass`.

### Task C: Next hot spot on small files

- [ ] Re-profile C1_PP_DWP and C4_CP_AUX_ESOC_MEP (`perf record -g`, 2000 serial loads). Write the top 15 symbols with
  % to `docs/perf/2026-11-round6-small-files.md` with a ranked list of what is left (header parser, column setup,
  mmap/munmap, Python wrapping — measure Python overhead as `cefast.load` time minus `bench_load` time).
- [ ] Implement only the top item if it is > 5 % and contained (one header or one function); counters; commit.

Report to the file named in the dispatch prompt: commits, counters before/after, fuzz runs (execs, time, findings),
anything reverted.
