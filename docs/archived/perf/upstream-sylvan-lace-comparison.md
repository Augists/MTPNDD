# MTPNDD on upstream Sylvan 1.10.0 + Lace 1.6.2 — performance study

Date: 2026-04-21
Worktree: `../mtpndd-upstream` on branch `feature/upstream-sylvan`

## Goal
Evaluate whether MTPNDD benefits from using upstream Sylvan + Lace directly,
instead of our vendored fork (currently Sylvan 1.9.1 + bundled Lace 1.5.0 plus
7 local perf commits).

## Setup
- **HEAD** = `feature/c` tip (`dbe423f`) = our fork (1.9.1 + 7 local perf commits
  + hugepage change), Lace 1.5.0 bundled.
- **upstream** = `feature/upstream-sylvan` tip with:
  - `sylvan/` git submodule → `Augists/sylvan` master → upstream `trolando/sylvan`
    at `cd9ccd4 Upgrade to Lace 1.6.2` (includes v1.10.0 + post-release commits).
  - Lace 1.6.2 fetched via Sylvan's CMake FetchContent.
  - MTPNDD code unchanged except for two adaptations required by Lace 1.6.0:
    1. Wrap the benchmark's work section in a `VOID_TASK_3` so `mtpndd_*`
       operations execute inside a Lace worker context (Lace 1.6 external-task
       dispatch hangs when RUN() is called repeatedly from the main thread).
    2. Flip shutdown order in `mtpndd_quit`: `sylvan_quit()` must run before
       `lace_stop()` in Lace 1.6 (workers need to stay alive for sylvan_quit's
       GC hooks).
- Benchmark: `mtpndd_nqueens_benchmark <n> <w>`, `MTPNDD_LOG_LEVEL=1`.
- System: THP=never, no hugepages reserved (hugepage-induced speedup isolated
  away — both binaries include the MAP_HUGETLB code with fallback, but with
  `nr_hugepages=0` fallback always triggers so neither gets a hugepage boost).

## Results (seconds)

| N  | W | HEAD   | upstream |  Δ%   |
|----|---|-------:|---------:|------:|
| 10 | 1 |  0.529 |    0.270 |  −49% |
| 10 | 2 |  0.367 |    0.265 |  −28% |
| 10 | 4 |  0.235 |    0.307 |  +31% |
| 10 | 6 |  0.181 |    0.381 | +111% |
| 11 | 1 |  2.429 |    1.246 |  −49% |
| 11 | 2 |  1.617 |    1.174 |  −27% |
| 11 | 4 |  0.947 |    1.363 |  +44% |
| 11 | 6 |  0.644 |    1.498 | +133% |
| 12 | 1 | 12.435 |    6.592 |  −47% |
| 12 | 2 |  8.382 |    5.784 |  −31% |
| 12 | 4 |  4.756 |    6.954 |  +46% |
| 12 | 6 |  3.105 |    7.914 | +155% |
| 13 | 1 | 72.799 |   39.331 |  −46% |
| 13 | 2 | 48.293 |   33.273 |  −31% |
| 13 | 4 | 27.392 |   39.867 |  +46% |
| 13 | 6 | 18.103 |   48.371 | +167% |

## Findings

1. **Single-worker path: upstream ~1.9× faster.** Consistent −46% to −49% across
   N=10..13. Attribute to Lace 1.6 task scheduling improvements and Sylvan
   1.10.0's cache/weak-memory work.
2. **Two-worker path: upstream ~1.4× faster** (−27% to −31%). Same trend,
   smaller margin.
3. **W=4: upstream ~1.4× slower** (+31% to +46%). Scaling cliff visible.
4. **W=6: upstream ~2.3× slower** (+111% to +167%). Scaling completely collapses
   on upstream without our per-worker refs/protect sharding.

The crossover between W=2 and W=4 is where global `mtbdd_refs` / `mtbdd_protect`
contention becomes the dominant cost on upstream, while our HEAD continues to
scale.

## Takeaways

- **Neither fork alone is dominant.** HEAD wins multi-worker; upstream wins
  serial and 2-worker.
- **The theoretical best** is upstream Sylvan 1.10.0 + Lace 1.6.2 + our
  per-worker refs (`50c738d`) and per-worker protect (`9806634`) rebased on
  top. Expected to combine upstream's ~1.9× single-worker win with HEAD's 2-4×
  multi-worker scaling.
- **Lace 1.6 migration cost is real but bounded.** Needed: (a) wrap benchmark
  work in a top-level TASK, (b) fix shutdown ordering, (c) drop
  `nqueens_parallel_benchmark` / `fraction_test` / `arith_benchmark` or
  migrate their `LACE_ME` / bare `CALL`/`SPAWN`/`SYNC` usage.
- **`sylvan_set_spawn_depth_cutoff` (our local `deb983f`) is not needed.**
  API-only; default already matches upstream behavior; its own commit message
  notes <1% difference on MTPNDD N-Queens.

## Next step

Phase 5: cherry-pick `50c738d` (per-worker refs) + `9806634` (per-worker
protect) from `feature/c` into the upstream submodule. Challenges:
- File layout differs (`src/sylvan/*.c` → `src/*.c`).
- Sylvan 1.9.2–1.9.4 made unrelated edits to `sylvan_mtbdd.c` (MtbddMap fix,
  memory leaks) — conflicts expected.
- MTPNDD references stats functions the mods add (`mtbdd_refs_merge_drop_total`
  etc.); those must be brought over too.

Estimated effort: 2–4 hours depending on conflict severity.
