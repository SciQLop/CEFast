# Round 5: where the parallel path starts to pay

After the speculative split (commit b98d157) `parse_records_parallel` no longer
pre-scans the body, so the cost of going parallel is: T thread spawns, T copies
of the variable list (`std::vector<schema> parts(chunks, result)`), and the
column concatenation afterwards. Those are fixed costs that dominate on small
files. This note measures where the fixed cost is amortised and sets
`detail::parallel_threshold` accordingly.

## Method

* `build-release/tools/bench/bench_load <file> 200 <threads>` (release -O3),
  200 loads per run, 3 runs, median of the per-run medians.
* `perf stat -e task-clock,instructions,cycles,branch-misses,page-faults` per
  run; wall alone is not trusted on this machine (other load), so
  instructions/load and task-clock/load are reported next to it.
* `threads=1` is the serial path (`parse_records`), `threads=0` is the default
  (8 threads, clamped to hardware).
* The crossover was measured with `parallel_threshold` temporarily set to 0 so
  that both paths could be timed on the same file.
* Synthetic files: `C1_CP_CIS-CODIF_HS_H1_CS` truncated at record boundaries
  (header kept, `!RECORDS` rewritten), so the body is exactly N KiB.

## Synthetic CODIF truncations (body size sweep)

| body | file | serial wall | parallel wall | speedup | insn serial/parallel | task-clock serial/parallel |
|---|---|---|---|---|---|---|
| 64 KiB | 78 KiB | 0.28 ms | 0.39 ms | 0.72x | 4.6M / 6.1M | 0.29 / 0.73 ms |
| 128 KiB | 147 KiB | 0.48 ms | 0.53 ms | 0.91x | 8.7M / 10.9M | 0.50 / 1.10 ms |
| 156 KiB | 171 KiB | 0.55 ms | 0.64 ms | 0.86x | 10.1M / 12.3M | 0.56 / 1.27 ms |
| 180 KiB | 194 KiB | 0.62 ms | 0.73 ms | 0.85x | 11.5M / 13.7M | 0.65 / 1.44 ms |
| 192 KiB | 205 KiB | 0.65 ms | 0.64 ms | 1.02x | 12.2M / 14.4M | 0.68 / 1.39 ms |
| 204 KiB | 217 KiB | 0.69 ms | 0.68 ms | 1.01x | 12.9M / 15.1M | 0.71 / 1.47 ms |
| 220 KiB | 240 KiB | 0.76 ms | 0.74 ms | 1.03x | 14.3M / 16.5M | 0.79 / 1.65 ms |
| 256 KiB | 275 KiB | 0.86 ms | 0.75 ms | 1.15x | 16.4M / 18.7M | 0.88 / 1.71 ms |
| 512 KiB | 529 KiB | 1.63 ms | 1.24 ms | 1.31x | 31.7M / 34.2M | 1.66 / 3.11 ms |

Break-even is at a ~190 KiB body. Below it parallel is slower on wall time
(and burns 40% more instructions for nothing, plus 2-3x the task-clock).
Note the parallel path always executes more instructions: the copy of the
variable list, the concatenation, and the thread setup are real work that
serial does not do. It only wins when that work is overlapped.

## Corpus files

| file | size | serial wall | parallel wall | speedup | insn serial/parallel | task-clock serial/parallel |
|---|---|---|---|---|---|---|
| C1_PP_DWP | 127 KiB | 0.36 ms | 0.62 ms | 0.58x | 5.8M / 9.4M | 0.39 / 0.95 ms |
| C4_CP_AUX_ESOC_MEP | 161 KiB | 0.47 ms | 0.51 ms | 0.92x | 8.5M / 10.6M | 0.48 / 1.05 ms |
| CC_CP_AUX_MAARBLE_KIR_ULF_PC35 | 245 KiB | 0.58 ms | 0.46 ms | 1.26x | 11.1M / 12.2M | 0.59 / 1.10 ms |
| C2_CP_PEA_PITCH_FIX_DEFlux | 309 KiB | 0.71 ms | 0.53 ms | 1.34x | 13.0M / 14.5M | 0.72 / 1.30 ms |
| D1_JP_AUX_PMP | 730 KiB | 1.55 ms | 0.74 ms | 2.09x | 27.9M / 29.2M | 1.58 / 2.30 ms |
| C1_JP_AUX_PMP | 756 KiB | 1.53 ms | 0.79 ms | 1.94x | 28.8M / 30.2M | 1.55 / 2.37 ms |
| C3_CP_EDI_EGD | 762 KiB | 1.83 ms | 0.82 ms | 2.23x | 34.0M / 35.1M | 1.85 / 2.68 ms |
| C3_CP_PEA_LERL_DEFlux | 847 KiB | 2.13 ms | 1.24 ms | 1.72x | 38.3M / 42.8M | 2.17 / 3.35 ms |
| C1_CP_CIS-CODIF_HS_He2_PEF | 1285 KiB | 3.93 ms | 2.20 ms | 1.79x | 78.1M / 81.2M | 4.01 / 6.73 ms |
| C1_CP_CIS-CODIF_HS_H1_CS | 1746 KiB | 5.37 ms | 1.29 ms | 4.16x | 104.9M / 108.3M | 5.41 / 6.57 ms |
| C1_CP_WHI_ELECTRON_DENSITY | 1918 KiB | 6.35 ms | 3.50 ms | 1.81x | 84.3M / 94.9M | 6.37 / 8.49 ms |

The corpus agrees with the synthetic sweep: the only file below 200 KiB
(C1_PP_DWP) loses badly, the 161 KiB one is a wash, and everything from
~245 KiB up wins.

## Decision

`parallel_threshold` goes from 512 KiB to 256 KiB: it sits ~35% above the
measured break-even, so a file must be comfortably in the winning regime before
the parallel path is taken, and it is a round number that is easy to reason
about. Files between 256 KiB and 512 KiB bodies now go parallel (they used to
be serial), which is where most of the benchmark gain below comes from.

Effect on `tools/ceflib/benchmark.py --runs 7` (whole corpus, default threads):

| | before (512 KiB) | after (256 KiB) |
|---|---|---|
| total cefast | 20.4 ms | 15.0 ms |
| total speedup vs CEFLIB | 1.90x | 2.30x |
| per-file median | 1.24x | 1.25x |
| per-file min | 0.83x | 0.88x |
| per-file max | 3.71x | 4.22x |
