# Upstream Sylvan 1.10.0 + Lace 1.6.2 + per-worker refs rewrite

Date: 2026-04-21
Worktree: `../mtpndd-upstream` on branch `feature/upstream-sylvan`
Submodule: `Augists/sylvan` branch `feature/mtpndd-perworker-refs`
(local only, not pushed)

## Context
A prior study (`upstream-sylvan-lace-comparison.md`) measured pristine
upstream Sylvan 1.10.0 + Lace 1.6.2 against our HEAD (feature/c) and found:
- upstream ~1.9× faster at W=1 (Lace 1.6 wins)
- upstream ~2.3× slower at W=6 (contention on global `mtbdd_refs` dominates)

Conclusion at the time: neither fork alone is dominant. The ideal is
upstream + our per-worker refs. This doc records the result of actually
doing that.

## Port summary
In the submodule (upstream Sylvan 1.10.0 at `cd9ccd4`):

1. Replaced `src/sylvan_refs.c` and `src/sylvan_refs.h` with the versions
   from our HEAD `feature/c`. Those support signed refs (`refs_modify`
   accepting an int delta), expose `refs_next_full` / `refs_set_add` /
   `refs_clear`, and add `refs_resize_total` / `protect_resize_total`.
   The new refs.c/h has **no Lace-specific code** so it drops into Lace
   1.6.2 unchanged.
2. Added `SYLVAN_REFS_INIT_SIZE` / `SYLVAN_PROTECT_INIT_SIZE` to
   `src/sylvan_config.h`.
3. Rewrote `mtbdd_refs` in `src/sylvan_mtbdd.c` to be a per-worker array:
   - `refs_table_t *mtbdd_refs` sized `lace_workers()` at init
   - `mtbdd_ref` / `mtbdd_deref` target the current worker's table
   - `mtbdd_gc_mark_external_refs` merges all worker tables into a
     dedicated `mtbdd_refs_merge` table using signed `refs_set_add`,
     then marks entries whose net count is > 0
   - Quit frees the array + the merge table

Total: **542 insertions, 33 deletions** across 4 files in the submodule.
MTPNDD itself was unchanged apart from the two Lace 1.6 adaptations done
earlier (wrap benchmark work in a top-level TASK; flip sylvan_quit /
lace_stop order in mtpndd_quit).

Per-worker *protect* (HEAD commit 9806634) was **not** ported — our
prior branch-to-branch measurement showed it contributes only 1-2% on
nqueens, and the data below confirms this config already beats HEAD
everywhere without it.

## Measurements (seconds)

mtpndd_nqueens_benchmark, `MTPNDD_LOG_LEVEL=1`, THP=never, nr_hugepages=0.

| N  | W | HEAD   | upstream+refs |  Δ%  |
|----|---|-------:|--------------:|-----:|
| 10 | 1 |  0.520 |         0.265 | −49% |
| 10 | 2 |  0.374 |         0.208 | −44% |
| 10 | 4 |  0.231 |         0.154 | −33% |
| 10 | 6 |  0.191 |         0.147 | −23% |
| 11 | 1 |  2.416 |         1.320 | −45% |
| 11 | 2 |  1.632 |         0.930 | −43% |
| 11 | 4 |  0.947 |         0.624 | −34% |
| 11 | 6 |  0.665 |         0.545 | −18% |
| 12 | 1 | 12.969 |         6.806 | −48% |
| 12 | 2 |  8.287 |         4.648 | −44% |
| 12 | 4 |  4.769 |         3.034 | −36% |
| 12 | 6 |  3.142 |         2.458 | −22% |
| 13 | 1 | 74.033 |        39.625 | −46% |
| 13 | 2 | 48.574 |        26.515 | −45% |
| 13 | 4 | 27.320 |        16.963 | −38% |
| 13 | 6 | 18.618 |        13.755 | −26% |

16/16 cells faster on upstream+refs than on HEAD, range −18% to −49%.
Correct solution counts (724 / 2680 / 14200 / 73712) on every run.

## What this means

- **Lace 1.6 single-core win is real and keeps most of its value at W>=2.**
  At W=1 and W=2 the improvement is roughly a flat ~45%, suggesting the
  gain comes from Lace task dispatch / scheduler cost, not from any
  contention effect. Sylvan 1.10.0's weak-memory cache tweaks likely also
  contribute.
- **per-worker refs is the only contention fix nqueens needs.** Without
  it, upstream collapses at W>=4. With it, upstream stays ahead of HEAD
  even at W=6. Per-worker protect adds nothing beyond noise.
- **Of our seven local perf commits, only one idea survived.** Six of
  them are either trivial (benchmark tweaks), turn out to be no-ops
  (spawn cutoff defaults to unlimited), MTPNDD-internal refactorings
  that don't interact with Sylvan, or low-ROI (per-worker protect).
  The per-worker refs commit (`50c738d`) is the only structural change
  that meaningfully moves nqueens numbers, and porting just that idea
  to upstream 1.10 yields ~2× on the single-threaded path and beats
  our current tuned fork everywhere.

## Migration recommendation

Switch the project to the submodule layout:
- `sylvan/` as git submodule tracking a personal fork of upstream
- `mtpndd/` at top level
- `jni/` unchanged
- Retire six of the seven local perf commits; carry only the per-worker
  refs rewrite as a patch on the sylvan submodule's branch.

Remaining work before a real migration:
- Port `nqueens_parallel_benchmark.c`, `fraction.c`, `arith_benchmark.c`
  to Lace 1.6 macros (`TASK(foo)` → `foo_CALL`, remove `LACE_ME`).
- Re-wire the JNI build to the new layout.
- Decide whether to also port per-worker protect as a second patch (the
  data suggests it is not worth the code).
