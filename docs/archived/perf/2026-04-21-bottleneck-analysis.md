# 2026-04-21 — Bottleneck analysis (post-migration)

Ran `perf stat` + `perf record -F 997 -g --call-graph=dwarf` on the
current `feature/c` tip (submodule layout, Release `-O3`,
`edge_bucket_count=8` default) for the reference workload
`mtpndd_nqueens_benchmark 12 6`.

## perf stat overview

Single N=12 W=6 run:

| Counter | Value | Derived |
|---|---|---|
| task-clock | 12.3 s across workers (wall 2.37 s) | 5.2× parallel efficiency / 6 workers |
| cache-references | 2.84 G | |
| cache-misses | 325 M | **11.5% miss rate** |
| branches | 5.45 G | |
| branch-misses | 73 M | 1.34% (low) |
| instructions | 25.4 G | |
| cycles | 42.9 G | **IPC 0.59** |
| context-switches | 0 | |

IPC 0.59 + 11.5% cache miss rate = workload is clearly memory-bound.
Not CPU-compute-bound, not branch-mispredict-bound, not syscall-bound.

## Hot functions (self %, `--no-children`)

| % | Symbol | Layer | Category |
|--:|---|---|---|
| 15.89 | refs_up | Sylvan | external-refs sharding |
| 11.98 | refs_down | Sylvan | external-refs sharding |
| 11.52 | find_node_in_nodetable | MTPNDD | nodetable lookup |
| 9.19 | mtpndd_and_rec_CALL | MTPNDD | AND recursion |
| 8.22 | mtpndd_memory_acquire_edge_entry | MTPNDD | slab |
| 5.62 | mtpndd_op_cache_lookup_binary | MTPNDD | op cache |
| 5.22 | mtpndd_memory_acquire_edge_map | MTPNDD | slab |
| 2.76 | mtpndd_is_terminal | MTPNDD | helper |
| 2.48 | mtpndd_ref | MTPNDD | API |
| 2.21 | sylvan_and_CALL | Sylvan | BDD op |
| 2.15 | pthread_spin_lock | libpthread | nodetable shard lock |
| 2.14 | mtpndd_and_two_phase_same_field | MTPNDD | AND variant |
| 1.98 | mtpndd_mk | MTPNDD | canonicalize |
| 1.86 | mtpndd_deref | MTPNDD | API |

Rolled up:

| Cluster | % |
|---|--:|
| Sylvan external refs (refs_up + refs_down) | 27.87 |
| MTPNDD hash / cache / core AND | 32.21 |
| Slab allocator acquire paths | ~14.5 |
| API thunks (ref/deref/temp_refs) | ~7.4 |
| Lace / pthread housekeeping | ~5.5 |

## Root-cause reading

1. **The per-worker refs sharding eliminated contention, but each
   ref/deref still runs a hash probe + atomic CAS.** 28% of CPU in the
   two functions is the dominant cost. MTPNDD protects every BDD edge
   label (213 `sylvan_ref(...)` call sites across `mtpndd_node.c` and
   `mtpndd_arith.c`), so the raw call frequency is enormous.
2. **MTPNDD core work (hash + AND recursion + op cache + mk) is ~32%.**
   This is the algorithmic baseline — canonicalization and recursive
   AND are what MTPNDD *is*. Hard to shrink without rethinking the
   node representation.
3. **Slab allocator acquire is ~14.5%.** Per-worker slab caches are
   already on; the remaining cost is the fast-path itself (pointer
   bump + occasional refill).
4. **Memory-bound characteristics** (IPC 0.59, 11.5% miss rate)
   suggest optimizations that improve locality will have more effect
   than raw algorithmic tweaks.

## Potentially under-explored optimizations

Ordered by estimated ROI. None have been A/B tested this session.

### 1. Shrink the `sylvan_ref` / `refs_up` hot path (target: 28%)

- **Inline `refs_up`/`refs_down`** — they're small, but the library
  boundary call overhead is ≥1% from the profile. Mark them
  `static inline` in the header.
- **Ref coalescing / label-cache**: detect when `sylvan_ref(x)` is
  called with an `x` that the current worker already holds, and skip
  the probe by using a tiny per-worker hot cache (a few recent BDDs
  in registers). Most nqueens AND fragments reference the same
  handful of labels repeatedly.
- **Lazy refs**: accumulate a per-operation list of "want-to-ref"
  handles and flush them in one batched insert at the operation
  boundary.
- **Widen existing `temp_refs` usage**: some call sites currently call
  `sylvan_ref` for values whose lifetime is local to an AND/OR merge
  — those could move to `temp_refs` and avoid the global table
  entirely.

### 2. Slab allocator fast path (target: 14.5%)

- Bump per-worker local cache capacities — today's defaults are
  `edge_entry/edge_map = 64/512`. Growing the cache trades memory for
  fewer refill paths.
- Consider thread-local storage (`_Thread_local`) instead of
  `g_slab_pools[worker_id]` indirection; may save one load per
  acquire.

### 3. Cache-line-conscious node layout (target: memory bound)

The 11.5% cache miss rate suggests node / edge-entry layout touches
cold lines on every hash probe. Worth a `pahole` or similar struct
layout pass, especially for `mtpndd_node_t` / `edge_bucket_entry_t`.

### 4. PGO / LTO (target: broad)

We're at `-O3` but no PGO and no LTO. Typical wins 5-15% on
pointer-heavy workloads. Easy to try.

### 5. NUMA / CPU pinning

`perf stat` showed 0 context switches and 0 cpu-migrations on this run
— Linux scheduler happened to keep workers put. Explicit
`sched_setaffinity` + NUMA-aware allocation could reduce cross-socket
cache misses on larger machines.

### 6. Op-cache sharding

Currently a single `mtpndd_op_cache`; 5.6% of samples on
`mtpndd_op_cache_lookup_binary`. Sharding it per-worker (like we did
for `mtbdd_refs`) is unlikely to help much because the cache is
probe-based (lock-free) and reads-heavy, but worth a one-shot test.

## Not worth chasing further (already ruled out)

- **Lace backoff tuning** — investigated in depth, unresolved ≤10% at
  W=6; accepted as migration cost (see same-day audit doc).
- **Per-worker protect** — tested, negative result on nqueens.
- **BDD spawn depth cutoff** — tested, <1% on nqueens (useful only for
  large-BDD workloads like sre-ndd BGP).
- **Hugepage** — tested, 1-5%, optional system-level tuning.

## Recommendation

If another perf push is on the table, tackle items 1 (refs hot path)
and 4 (PGO/LTO) in that order. Both are locally testable with a
well-defined A/B. Item 2 (slab caches) is next. Items 3, 5, 6 are
speculative without more measurement.
