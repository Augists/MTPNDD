# MTBDD per-worker refs (signed counts + GC merge)

## Goal
Reduce contention in `refs_modify` by sharding MTBDD external refs per worker while preserving correctness via GC-time merge. Allow per-worker negative counts (cross-worker ref/deref order) and decide roots by global net count > 0.

## Implementation summary
- `refs_table_t` now stores **signed** 24-bit counts; `refs_down` may create negative entries.
- `mtbdd_refs` is an **array of refs tables** sized by `lace_workers()`.
- `mtbdd_ref/deref` write into the current worker table.
- GC merges all per-worker tables into a persistent `mtbdd_refs_merge` table, then marks only `net > 0` keys.

## Benchmark setup
- Binary: `mtpndd_nqueens_benchmark <n> <workers>`
- Build: `cmake -B build -DMTPNDD_LOG_LEVEL=0 && cmake --build build`
- Workers: 1–4
- Sizes: N=8, N=12

## Results
### N=8
| workers | time(s) | solutions |
|---:|---:|---:|
| 1 | 0.033 | 92 |
| 2 | 0.027 | 92 |
| 3 | 0.023 | 92 |
| 4 | 0.022 | 92 |

### N=12
| workers | time(s) | solutions |
|---:|---:|---:|
| 1 | 11.849 | 14200 |
| 2 | 7.523 | 14200 |
| 3 | 5.824 | 14200 |
| 4 | 5.647 | 14200 |

## Deltas vs prior baseline (plot_data.md “N-Queens runtime”)
### N=8
| workers | baseline(s) | new(s) | speedup (x) | reduction (%) |
|---:|---:|---:|---:|---:|
| 1 | 0.094 | 0.033 | 2.85 | 64.9 |
| 2 | 0.060 | 0.027 | 2.22 | 55.0 |
| 3 | 0.057 | 0.023 | 2.48 | 59.6 |
| 4 | 0.057 | 0.022 | 2.59 | 61.4 |

### N=12
| workers | baseline(s) | new(s) | speedup (x) | reduction (%) |
|---:|---:|---:|---:|---:|
| 1 | 12.128 | 11.849 | 1.02 | 2.3 |
| 2 | 9.359 | 7.523 | 1.24 | 19.6 |
| 3 | 8.455 | 5.824 | 1.45 | 31.1 |
| 4 | 8.308 | 5.647 | 1.47 | 32.0 |

## Notes
- The benchmark prints only elapsed time and solutions, so this dataset is directly comparable across runs.
- The per-worker refs approach improves scaling on N=12 under 2–4 workers compared to earlier baselines in `plot_data.md`.
- N=8 deltas compare `mtpndd_nqueens_test` baseline vs `mtpndd_nqueens_benchmark` new results; include this caveat if used in the paper.
- GC merge currently uses a simple open-addressing add (`refs_set_add`) with 128-probe limit; if this shows merge saturation in future runs, we should consider resizing or a longer probe budget.
