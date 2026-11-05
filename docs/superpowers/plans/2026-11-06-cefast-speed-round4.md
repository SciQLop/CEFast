# CEFast speed round 4 — SIMD structural scan and ISO time

Date: 2026-11-06. Plan for one round; measurement decides what stays.

## Goal

Cut instructions on the serial parse. The round-3 counters settled the diagnosis: CODIF parses at
~135 M instructions per load (~229 per value, ~76 per byte), IPC ~4, branch-miss rate 0.16 %.
That is instruction-bound, not branch- or memory-bound, so the win is doing fewer instructions per
byte and per value — which is what SIMD gives, and what micro-tuning of branches would not.

Primary metric: `instructions:u` per load from `perf stat -r N` on `bench_load <file> N 1`
(threads = 1; the parallel path would hide the serial cost by summing threads).
Files: CODIF `C1_CP_CIS-CODIF_HS_H1_CS`, WHI `C1_CP_WHI_ELECTRON_DENSITY`, and one small file
(`C1_CP_EDI_MP`, 0.09 MB) so header-dominated work is not ignored.
Secondary metric: wall time, same benchmark script as rounds 1-3.

## Global constraints

- No existing test edited or deleted; additions only.
- Every commit message carries the instruction counts and the benchmark line
  (`total: cefast X ms, CEFLIB Y ms, speedup Zx; CODIF: a ms vs b ms`).
- A change inside ±3 % of noise is reverted and reported.
- Every SIMD path keeps a scalar twin, selected at run time, plus a corpus-wide differential test
  comparing every column of every file between the two paths.
- Header-only: no runtime dispatch in this round. The baseline ISA only (SSE2 on x86_64, NEON on
  aarch64, simd128 on wasm when the build enables it, scalar elsewhere). AVX2/AVX-512 dispatch is
  a proposal with numbers, not code.
- Never push, tag or upload.

## Task 1 — structural scan in blocks

`include/cefast/data/structural.hpp` gains a block scanner: classify 64 bytes at a time into
bitmasks for `,`, the single-character marker, `"`, `!`, `\n` and whitespace, then derive quote
regions (prefix XOR of the quote bits: carry-less multiply where the ISA has it, otherwise the
shift-xor cascade) and comment regions (from a `!` outside quotes to the end of the line), and walk
only the interesting bits with `std::countr_zero`. The existing per-byte table stays as the scalar
twin behind `scan_one_byte_at_a_time()`.

The tail (fewer than 64 bytes left) is copied into a zero-padded scratch block so no load crosses
the end of the input. Multi-character markers keep the existing byte-by-byte path.

Measure: instructions per load on the three files, then the corpus.

## Task 2 — SIMD ISO time

A fixed 24-byte `YYYY-MM-DDThh:mm:ss.fffZ` fast path: validate digits, separators and ranges with
vector compares, compute days-from-civil on four lanes, then scale to nanoseconds. Anything else
(some other fraction width, no fraction, `:60`, out-of-range values, no `Z`) falls back to the
scalar parser, which stays the reference. Differential test: every corpus time, both paths, equal
or both rejected.

## Task 3 — whatever the instruction profile says

After 1 and 2, `perf record -e instructions:u` on the three files; if a single site still dominates
(per-value work around `fast_float`, `store`, `add`), attack that with the same rules, or write down
why it cannot be reduced.

## Verification

- Full suite 9/9, ASan suite 7/7, TSan corpus run (binary directly, meson's 30 s default is short).
- Wheels still build: xsimd through the wrap, pyodide compiles (scalar or simd128).
- Per-file table against CEFLIB 1.8.6 at the end of the report.
