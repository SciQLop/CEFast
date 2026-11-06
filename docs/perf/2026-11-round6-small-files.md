# Round 6: small files after the one-pass header lexer

Round 6 replaced the three walks of every header line (scan, find `=`, split on commas) with a
single pass (`7e18dde`, `perf(header): lex each line in one pass`). This note re-measures what is
left on the two files that were still slower than CEFLIB, C1_PP_DWP (130 KB, a 70 KB header over
9504 values) and C4_CP_AUX_ESOC_MEP (165 KB, 11883 values), and ranks the next candidates.

The header is no longer the lever: `lex_header` and `scan_line` together are 8.8% of a C1_PP_DWP
load and 3.3% of a C4 load, where `lex_header` alone was 16.6% and `split_values` 12.5% before the
round.

## Method

* `build-release/tools/bench/bench_load <file> 2000 1` (release -O3, serial parser), the same
  entry point as `cefast.load` minus the Python wrapping.
* `perf record -g` on that command (2000 loads, 2675 samples for C1_PP_DWP, 3684 for C4),
  `perf report --no-children` for self time. Percentages are cycles.
* Wall time is the median of 2000 loads in a fixed order; the machine is shared, so instructions
  are the primary signal and one decimal place is all the resolution the data deserves.
* Python overhead is `cefast.load` measured in the venv (2000 loads, min and median) minus the
  same load through `bench_load`, so it carries the whole pybind11 result conversion.

## C1_PP_DWP (130 KB, median 0.32 ms/load)

| self | symbol |
|---|---|
| 26.95% | `detail::record_scanner::run()` |
| 12.34% | `detail::record_scanner::add(...)` |
| 6.13% | `lex_header(...)` |
| 5.73% | `detail::parse_float<float>(...)` |
| 4.62% | `__memchr_avx2` |
| 4.09% | `std::from_chars<long>` |
| 3.91% | `detail::trim(std::string_view)` |
| 3.26% | `_int_malloc` |
| 3.04% | `main` (the benchmark loop) |
| 2.70% | `detail::scan_line(...)` |
| 2.37% | `parse_iso_time(...)` |
| 2.13% | `__memcmp_avx2_movbe` |
| 1.59% | `cfree` |
| 1.49% | `malloc` |
| 1.41% | `detail::parser_state::handle_attribute(...)` |

Below the cut: `lexed_header::~lexed_header()` 1.28%, `_Sp_counted_base::_M_release()` 1.11%,
`detail::parse_int` 1.03%, `string_view::compare` 0.92%, `deque<string>::emplace_back` 0.91%.

## C4_CP_AUX_ESOC_MEP (165 KB, median 0.44 ms/load)

| self | symbol |
|---|---|
| 47.45% | `detail::record_scanner::run()` |
| 16.15% | `detail::parse_float<float>(...)` |
| 12.04% | `detail::record_scanner::add(...)` |
| 4.13% | `parse_iso_time(...)` |
| 2.53% | `lex_header(...)` |
| 1.60% | `detail::trim(std::string_view)` |
| 1.52% | `__memchr_avx2` |
| 1.33% | `__memcmp_avx2_movbe` |
| 1.31% | `_int_malloc` |
| 1.14% | `detail::parser_state::handle_attribute(...)` |
| 1.04% | `main` (the benchmark loop) |
| 0.85% | `malloc` |
| 0.80% | `detail::scan_line(...)` |
| 0.66% | `lexed_header::~lexed_header()` |
| 0.60% | `string_view::compare` |

Below the cut: `detail::two_digits` 0.49%, `cfree` 0.48%.

## Where the header goes now

On C1_PP_DWP the whole header path is `lex_header` 6.13% + `scan_line` 2.70% +
`handle_attribute` 1.41%, plus its slice of `trim` (the lexer calls it per key and per value) and
of `memchr`/`memcmp` (finding quotes, commas and attribute names). Call it ~12%, and note that it
scales with the header, not with the data: on C4 (13 more records) it is ~4%. A further header
rewrite would have to win under 10% of a load, so the remaining cost is in the record body.

## What is left, ranked

1. **`record_scanner::run()` — 27% / 47%.** The stop search is the hot instruction in the
   function: `perf annotate` puts 22% of the function's cycles on the `stop_table` lookup branch
   and 10% on the table load, i.e. the per-byte traversal, not the dispatch. A stop lands every
   ~6 bytes on these files (14 bytes per value on C4, 6 on C1_PP_DWP), so 8-byte skipping barely
   pays: tried in this round, two variants of a word-at-a-time stop search (SWAR comparisons per
   word, a table check per flagged byte) cost 11-17% more instructions on C1_PP_DWP and 24-35%
   more on C4, cycles following, and were reverted. The next attempt has to attack
   the branch rather than the loop bounds: a jump-table dispatch on the stop byte (`switch`) or a
   table that returns the action instead of a bool. `scan_one_byte_at_a_time()` stays as the
   reference traversal for the corpus test.
2. **`record_scanner::add()` — 12% / 12%.** Per value: a slot lookup, then `store()` switches on
   the value type although a given column has one type for the whole file. A per-slot function
   pointer (or a stored parse lambda) built once in the constructor would replace the switch and
   the `std::variant` visit by one indirect call; the type switch also re-reads `state.has_fill`
   for every value.
3. **`parse_float` — 5.7% / 16%.** A small-integer shortcut, then `fast_float::from_chars`; the
   cost is per value, and only a batched parse (a fixed-format scan of a whole record) would move
   it.
4. **Header path — ~12% / ~4%.** `lex_header`, `scan_line`, `handle_attribute`, `trim`, and the
   `deque<string>` key/join traffic. Tiny on C4; on C1_PP_DWP the header is 54% of the file, so
   this is the file where any further header win would show.
5. **Allocation — ~6.3% / ~3.6%.** `_int_malloc`, `malloc`, `cfree`, `unlink_chunk`: the
   `lexed_header` strings (`keys`, `joined`, one `std::string` per header line via the shared_ptr
   source) and the column buffers. Dropping the per-line `shared_ptr<const std::string>` copy is
   1.1% (`_M_release`) and would widen the `header_line` interface; not taken.
6. **`memchr`/`memcmp` — 6.7% / 2.9%.** Finding the next quote or comma in the header, and
   comparing attribute names. Consequence of 4 and 5 rather than a target of its own.
7. **mmap/munmap — under the sampling floor.** No kernel symbols in either report; the mapping is
   touched once (page-faults 5.8 per load on C1_PP_DWP, 3.1 on C4).

## Python overhead

| file | `bench_load` min / median | `cefast.load` min / median | overhead |
|---|---|---|---|
| C1_PP_DWP | 0.30 / 0.32 ms | 0.314 / 0.329 ms | 14 µs (4.4%) |
| C4_CP_AUX_ESOC_MEP | 0.42 / 0.44 ms | 0.462 / 0.476 ms | 42 µs (10%) |

The gap is the pybind11 conversion of the result (a Python object per variable and the arrays
behind them), not call overhead: on a 0.3 ms load the wrapper costs what the whole header parse
costs. A `--runs 7` cefast-vs-CEFLIB run after the lexer commit: total 14.7 ms vs 34.0 ms,
per-file median 1.33x, min 0.91x (C4).

## After round 6 (2026-11-06, early morning)

Per load, `perf stat bench_load <file> 500 1`, instructions.

**Kept — consume a run of padding spaces in one step (`79897be`).** C4_CP_AUX_ESOC_MEP pads each value with
5-13 spaces and the scanner went round its main loop once per space. C4 8.40M -> 5.74M (-32 %, cycles -25 %),
PEA 38.0M -> 34.5M (-9 %), CODIF H1 -1 %, DWP and WHI flat. A first version tested `is_space()` (three
compares) in the inner loop and cost single-blank files +2-4 %; testing `' '` only removed that.

**Reverted — small-integer shortcut in `parse_int`.** DWP has 26 INT values per record, so it looked free:
DWP 5.12M -> 4.89M (-4.5 %). But CODIF H1 (FLOAT only) went 103.9M -> 107.4M (+3.4 %, deterministic) and
PEA +1.6 %: the extra code changed GCC's inlining in `record_scanner::run()` (its own share went 37M -> 44M
instructions) though `run()` never calls `parse_int`. Next attempt should pin the inlining first (e.g. keep
`store()` out of line, or `[[gnu::noinline]]` on the cold CHAR/time cases) and re-measure all files.
