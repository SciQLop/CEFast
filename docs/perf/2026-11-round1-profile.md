# CEFast round-1 profile (2026-11, after speed tasks 1-3)

Machine: AMD Ryzen 7 5800X (8 cores / 16 threads), 62 GB RAM, Fedora 44, idle otherwise.
Build: `meson setup --buildtype=release -Dwith_tests=false build-release` (`-O3`), HEAD `153cbe7`.
Timings are the **minimum of N passes** after one warm-up: this box drifts 5-10 % on sustained
load, so minima are the only comparable numbers. `perf record -g` was run on
`tools/bench/bench_load.cpp` (committed, `-Dwith_benchmarks=true`), 200 iterations per file.

## Whole-corpus benchmark (41 CSA files, cefast vs CEFLIB 1.8.6)

| file | MB | cefast ms | CEFLIB ms | speedup | cefast MB/s |
|---|---|---|---|---|---|
| C1_CP_CIS-CODIF_HS_H1_CS__20020202_000000_20020202_003000_V151020.cef | 1.70 | 11.61 | 7.25 | 0.62× | 146 |
| C1_CP_CIS-CODIF_HS_He2_PEF__20020202_000000_20020202_003000_V151022.cef | 1.26 | 8.49 | 5.15 | 0.61× | 148 |
| C1_CP_CIS-CODIF_LS_RATES__20020202_000000_20020202_060000_V100705.cef | 0.02 | 0.18 | 0.14 | 0.81× | 111 |
| C1_CP_CIS-CODIF_RPA_H1_PF__20020202_000000_20020202_003000_V150711.cef | 0.02 | 0.15 | 0.13 | 0.87× | 133 |
| C1_CP_EDI_MP__20050610_000000_20050610_001000_V180117.cef | 0.09 | 0.51 | 0.27 | 0.53× | 176 |
| C1_CP_EFW_L3_E__20050610_000000_20050610_002000_V220603.cef | 0.05 | 0.26 | 0.17 | 0.64× | 192 |
| C1_CP_FGM_SPIN__20050610_000000_20050610_002000_V140305.cef | 0.04 | 0.25 | 0.17 | 0.68× | 160 |
| C1_CP_PEA_3DXPH_DEFlux__20011001_000000_20011001_120000_V090915.cef | 0.04 | 0.33 | 0.28 | 0.86× | 121 |
| C1_CP_WHI_ELECTRON_DENSITY__20020202_000000_20020202_120000_V100209.cef | 1.87 | 8.70 | 7.53 | 0.87× | 215 |
| C1_CQ_WHI_CAVEATS__20020202_000000_20020202_120000_V220129.cef | 0.01 | 0.09 | 0.08 | 0.83× | 111 |
| C1_CT_AUX_ESOC_GSU_LOG__20050101_000000_20050131_000000_V261014.cef | 0.01 | 0.09 | 0.08 | 0.84× | 111 |
| C1_JP_AUX_PMP__20050101_000000_20050131_000000_V131031.cef | 0.74 | 3.53 | 1.71 | 0.49× | 210 |
| C1_PP_DWP__20050610_000000_20050610_002000_V100331.cef | 0.12 | 0.64 | 0.34 | 0.53× | 187 |
| C1_PP_EDI__20050610_000000_20050610_002000_V071205.cef | 0.06 | 0.38 | 0.22 | 0.58× | 158 |
| C1_PP_FGM__20010606_000000_20010606_002000_V051014.cef | 0.05 | 0.30 | 0.19 | 0.61× | 167 |
| C1_PP_STA__20050610_000000_20050610_002000_V090213.cef | 0.08 | 0.44 | 0.25 | 0.57× | 182 |
| C2_CP_PEA_PITCH_FIX_DEFlux__20050610_000000_20050610_000200_V210919.cef | 0.30 | 1.54 | 0.69 | 0.45× | 195 |
| C2_CQ_EFW_INST__20050101_000000_20051231_000000_V260311.cef | 0.02 | 0.13 | 0.10 | 0.80× | 154 |
| C2_PP_ASP__20050610_000000_20050610_003000_V071214.cef | 0.04 | 0.24 | 0.17 | 0.68× | 167 |
| C2_PP_FGM__20050610_000000_20050610_002000_V090213.cef | 0.05 | 0.30 | 0.18 | 0.61× | 167 |
| C3_CP_EDI_EGD__20050610_000000_20050610_002000_V220916.cef | 0.74 | 3.41 | 2.17 | 0.64× | 217 |
| C3_CP_PEA_LERL_DEFlux__20020610_000000_20020610_001000_V090810.cef | 0.83 | 4.22 | 2.13 | 0.51× | 197 |
| C3_CP_RAP_DSETTINGS__20050610_000000_20050610_000500_V191029.cef | 0.02 | 0.15 | 0.12 | 0.84× | 133 |
| C3_CP_RAP_PED_BM__20050610_000000_20050610_000500_V151120.cef | 0.02 | 0.13 | 0.11 | 0.85× | 154 |
| C3_CT_AUX_GRMB__20050101_000000_20050131_000000_V261005.cef | 0.06 | 0.28 | 0.24 | 0.86× | 214 |
| C3_CT_WBD_COVERAGE2__20050101_000000_20050131_000000_V250522.cef | 0.02 | 0.15 | 0.14 | 0.91× | 133 |
| C3_PP_CIS__20050610_000000_20050610_002000_V071216.cef | 0.08 | 0.48 | 0.28 | 0.59× | 167 |
| C4_CP_ASP_IONC__20050610_000000_20050610_002000_V071005.cef | 0.09 | 0.45 | 0.36 | 0.78× | 200 |
| C4_CP_AUX_ESOC_MEP__20050610_000000_20050610_010000_V231018.cef | 0.16 | 0.80 | 0.44 | 0.56× | 200 |
| C4_CP_WHI_ELECTRON_DENSITY__20050610_000000_20050610_002000_V100224.cef | 0.02 | 0.14 | 0.14 | 0.99× | 143 |
| C4_CQ_CIS-CODIF_CAVEATS__20050101_000000_20051231_000000_V110221.cef | 0.01 | 0.10 | 0.10 | 0.99× | 100 |
| CL_SP_ASP__20050610_000000_20050610_020000_V071217.cef | 0.02 | 0.17 | 0.11 | 0.67× | 118 |
| CL_SP_AUX__20050610_000000_20050610_020000_V090505.cef | 0.09 | 0.56 | 0.40 | 0.70× | 161 |
| CM_CP_FGM_UT_PIOR__20030610_000000_20030610_060000_V250919.cef | 0.01 | 0.11 | 0.11 | 1.03× | 91 |
| D1_CP_PEA_3DXHAR_DEFlux__20050115_000000_20050115_235900_V240723.cef | 0.04 | 0.33 | 0.27 | 0.84× | 121 |
| D1_JP_AUX_PMP__20050101_000000_20050131_000000_V150319.cef | 0.71 | 3.39 | 1.68 | 0.50× | 209 |
| D1_PP_HIA__20050610_000000_20050610_002000_V150320.cef | 0.05 | 0.32 | 0.20 | 0.62× | 156 |
| D1_SP_AUX__20050610_000000_20050610_020000_V150319.cef | 0.03 | 0.24 | 0.16 | 0.69× | 125 |
| D1_SP_FGM__20050610_000000_20050610_020000_V150411.cef | 0.03 | 0.19 | 0.14 | 0.74× | 158 |
| D2_CP_FGM_SPIN__20050610_000000_20050610_002000_V170322.cef | 0.04 | 0.27 | 0.23 | 0.83× | 148 |
| CC_CP_AUX_MAARBLE_KIR_ULF_PC35__20030610_000000_20030610_060000_V141119.cef | 0.24 | 1.22 | — (CEFLIB cannot read it) | — | 197 |

Totals: **cefast 54.0 ms, CEFLIB 34.6 ms (0.64x)**; per-file speedup median 0.68x, min 0.45x,
max 1.03x. `--gzip` total (in-memory `.cef.gz`): 57.6 ms, i.e. +6 % over plain; that is gzip
decompression, and CEFLIB has no in-memory entry point to compare against.

Where the time goes is per-value, not per-byte: large many-value records are the slow ones
(CODIF 8x16x31 at 0.62x, D1_JP_AUX_PMP 0.62x), while small scalar files are at parity or above
(1.03x). CEFLIB wins by roughly 1.6x on exactly the files where a record holds thousands of
numbers.

## Profiles (user space, children excluded)

Driver numbers here sit ~10 % above the table's per-file medians measured from Python: the table is
a median of 5 short runs per file, these are minima of 200 in one process (see item 4).

CODIF (1.70 MB, 592 392 values, 149 records x 3975): 13.14 ms, 129.7 MB/s, 22.2 ns/value.

| symbol | % |
|---|---|
| `record_scanner::run()` | 44.40 |
| `append_value(column&, value_type, ...)` | 34.38 |
| `__memcmp_avx2_movbe` (marker comparison) | 9.45 |
| `record_scanner::add(string_view, bool)` | 9.03 |
| `__memchr_avx2` | 0.54 |

WHI (1.87 MB, 244 100 values, 24 410 records): 11.72 ms, 159.8 MB/s, 48.0 ns/value.

| symbol | % |
|---|---|
| `record_scanner::run()` | 48.15 |
| `append_value(column&, value_type, ...)` | 13.00 |
| `__memcmp_avx2_movbe` (marker comparison) | 10.35 |
| `parse_iso_time` | 5.29 |
| `record_scanner::add(string_view, bool)` | 4.05 |
| `__memchr_avx2` | 3.48 |
| `std::string` construction (CHAR copies) | 3.07 |

Header only (C1_CP_PEA_3DXPH_DEFlux, 0.04 MB, 0 records): 0.91 ms, 45 MB/s.

| symbol | % |
|---|---|
| `lex_header`/`parse_line` | 24.13 |
| `_int_malloc` + `malloc` | 11.95 |
| `vector<header_line>` growth | 4.99 |
| `memmove` (vector relocations) | 4.21 |

## The five targeted measurements

1. **Per-character record-marker comparison costs 36-37 % of the parse.** The scanner asks
   `data.compare(position, marker.size(), marker)` for *every* character, which is an out-of-line
   `memcmp` per byte (9.45 % of CODIF, 10.35 % of WHI sampled there, plus the register pressure it
   adds to `run()`). Replacing it with a one-character fast path
   (`marker.size() == 1 ? c == marker.front() : data.compare(...)`) - semantically identical - gave
   **CODIF 13.14 -> 8.37 ms (-36 %), WHI 11.72 -> 7.41 ms (-37 %)**, min of 100. Reverted: round 1
   is measurement only.
2. **Per-value column dispatch is free.** Over the 592 275 CODIF tokens: `append_value` (variant
   `std::get` + `value_type` switch + parse + `push_back`) 2.72 ms (4.6 ns/token) vs a hand-typed
   `fast_float` + `push_back` loop 2.89 ms (4.9 ns/token). The difference is inside the noise: the
   compiler inlines the switch, so `std::visit`/variant indirection is not worth optimising.
3. **fast_float alone is ~20 % of the CODIF parse.** 592 275 tokens in 2.57 ms = **4.3 ns/token**,
   i.e. 2.6 ms of the 13.1 ms file. A correctly-rounded parser cannot be made much cheaper; the
   remaining ~10 ms is the scanner.
4. **Python costs nothing measurable.** Same file, same build: Python `cefast.load()` min 10.81 ms
   (WHI) and 11.91 ms (CODIF) vs the C++ driver 11.75 and 13.14 ms. The pybind11 boundary plus the
   `CEF`/`Variable` wrappers are inside run-to-run noise (Python even looks 8 % faster, which is code
   layout). No Python-side optimisation is worth a plan.
5. **I/O is not the bottleneck.** A bare `memchr` pass over the file runs at **12.8 GB/s** for
   newlines and **756 MB/s** for the 592 k commas (3.8 ns per comma); the parser runs at 130-160 MB/s.
   Even a scalar separator scan is 5x faster than the current parse, so the gap is per-value work,
   not bytes.

## Round 2, ranked by measured gain

1. **One-character marker fast path** (`records.hpp`, ~1 line). -36 % / -37 % on the two profiled
   files, no behaviour change, no new test surface (the existing record suite covers both marker
   kinds). Do this first and re-profile; it changes every number below.
2. **Structural scan of the record body** (simdjson-style separator/quote/comment bitmaps or a
   `memchr`-driven token walk). `run()` is still 44-48 % of the profile and the separator search
   alone runs at 756 MB/s vs 130 MB/s end-to-end, so a structural pass plus the unchanged
   `append_value` should recover a large part of the remaining gap. Measure after (1): if `run()`
   is still >40 %, this is worth it; if it collapses below ~15 %, stop.
3. **Header parsing without materialising `header_line`/`token` vectors.** A 0.04 MB header-only
   file takes 0.91 ms (45 MB/s) with ~16 % of it in `malloc`/`memmove` from vector growth; across
   the corpus that is ~0.3-0.9 ms per file, 12-20 % of the 54 ms total, and it is the dominant cost
   for the small files speasy loads one by one.
4. **CHAR storage as one fixed-width buffer per variable** instead of `std::vector<std::string>`.
   WHI spends 3.07 % in `std::string` construction and another slice of its 13 % `append_value` on
   CHAR copies (6 CHAR variables x 24 410 records). Only files with CHAR columns benefit; the
   change is local to the column variant and the Python `str_` copy.
5. **SIMD ISO time parsing**: `parse_iso_time` is 5.29 % of WHI (one time per record, 24 410 of
   244 100 values) and less elsewhere. A SIMD fixed-width parser can win at most that ~5 %, so it
   comes after the four items above.
6. **Parallel chunk parsing**: after (1) a 1.7 MB file parses in 8.4 ms single-threaded, and the
   work splits cleanly at record markers. It is the only item that can show a >2x wall-clock win,
   but it multiplies complexity for the files that are already at parity (small scalar files) and
   does nothing for the per-value cost CEFLIB beats us on. Gate it on (1) and (2) landing and on a
   real user waiting on multi-second files.

Not worth doing, with the number that says so: anything about the column dispatch (0.0 ns/token
measured), Python-side batching (0 % measured), mmap/read changes (I/O ceiling 12.8 GB/s vs
130 MB/s actual), and a faster float parser (4.3 ns/token is already a large share of the ideal).

## After the marker fast path

Same machine, `build-release`, min of 200 loads (`bench_load`), after `perf(records): compare
single-character markers with ==`.

| file | min | MB/s | ns/value |
|---|---|---|---|
| C1_CP_CIS-CODIF_HS_H1_CS (592 392 values) | 8.49 ms | 200.7 | 14.3 |
| C1_CP_WHI_ELECTRON_DENSITY (244 100) | 7.60 ms | 246.3 | 31.2 |
| CM_CP_FGM_UT_PIOR (header only, 13 958 B) | 0.09 ms | 140.2 | — |

Self time, `perf report --no-children`:

| symbol | CODIF | WHI | header-only |
|---|---|---|---|
| `record_scanner::run()` | 34.6 % | 36.7 % | — |
| `record_scanner::add()` | 10.6 % | 4.9 % | — |
| `append_value()` | 50.3 % | 18.1 % | — |
| `parse_iso_time()` | — | 8.8 % | — |
| `std::string` move ctor (CHAR cells) | — | 6.0 % | — |
| `lex_header` + `parse_line` | 0.5 % | 0.6 % | 33.6 % |
| `malloc`/`free`/`memmove` | 0.5 % | — | 31.9 % |
| `vector<header_line>::push_back` | — | — | 4.5 % |

The marker change moved `run()` from 44.4 % to 34.6 % on CODIF and 48.2 % to 36.7 % on WHI, and
`memcmp` from 9.5 %/10.4 % to 0.5 %: the per-character comparison is gone as a cost, and the
remaining `run()` is the per-byte state machine around the separator and the token end.

### Round 2 order, by measured share

1. **Task C, structural scan** — `run()` + `add()` is 45 % of CODIF and 42 % of WHI, and these
   are the files where CEFLIB still wins (min 0.64x). Largest absolute win.
2. **Task B, header without per-line vectors** — 46 % of a header-only load (`lex_header`,
   `parse_line`, `malloc`, `memmove`, `push_back`), which is the small-file latency speasy pays
   one file at a time. Second because its absolute cost is small on the big files (0.5 %).
3. **Task D, fixed-width CHAR** — 6 % `std::string` construction plus CHAR's slice of
   `append_value`, on WHI only; no other corpus file moves it.

Nothing reaches the < 5 % skip rule: every hot spot above is ≥ 6 % on at least one profiled file.
One correction to the round-1 ranking: `parse_iso_time` is now **8.8 %** of WHI, not 5.29 %, so
SIMD ISO time is no longer below the 5 % bar. It stays out of this round (it is one value per
record and Task C/D change the surrounding cost), but it is a legitimate round-3 item.
