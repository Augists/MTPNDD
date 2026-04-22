# Plan: open-addressing nodetable (future work)

Status: **design note, not started**. Captured as the next-big-thing
if/when we decide to chase more performance on `find_node_in_nodetable`.

## Motivation

After the 2026-04-21 optimization pass (per-worker refs sharding, ref
coalescing cache, slab in_use per-worker, misc inlining), the hot
function distribution at N=12 W=6 is:

| % | Symbol | Layer |
|--:|---|---|
| 19.41 | `mtpndd_and_rec_CALL` | algorithm |
| 18.69 | `find_node_in_nodetable` | nodetable |
| 10.42 | `mtpndd_op_cache_lookup_binary` | op cache (inlined) |
|  5.70 | `mtpndd_and_two_phase_same_field` | algorithm |
|  4.70 | `mtpndd_mk` | nodetable insert |
|  4.27 | `pthread_spin_lock` | per-shard bucket lock |

`find_node_in_nodetable` is ~19% and the nodetable is a **chained**
hash table today. Each lookup:
1. Acquires the per-shard spinlock (`pthread_spin_lock` shows ~4% on
   its own).
2. Computes the bucket index.
3. Walks a singly-linked list of `mtpndd_nodetable_bucket_entry_t` (40
   bytes each after the cached_hash mirror) — each node touch is a
   potentially cold cacheline.
4. For each entry that survives the cached_hash fast-reject, calls
   `nodetable_edges_equal` which walks the edge map.

The bucket list is short on nqueens (pre-allocated buckets give ~0.7%
load factor at N=12) so the walk usually ends in 0-1 steps. Most cost
is the lock + the first cacheline load on the entry. For denser
workloads the list walk dominates.

## Target design

Open-addressing (linear or quadratic probing) hash table. Advantages:

- **Cacheline-friendly probes.** Consecutive slots live in the same /
  adjacent lines, so after one miss the next probe hits warm cache.
  Chained lists scatter entries across the arena.
- **No per-node `next`/`prev` pointers.** Each slot is just the payload
  (`edges *`, `node *`, cached_hash = 24 bytes), so we can fit ~2.7
  slots per cacheline vs ~1.6 bucket entries today.
- **No per-slot allocator.** Slots are a flat array, so
  `mtpndd_memory_acquire_nodetable_entry` (3% self) disappears too.
- **Lock-free lookups** are easier (atomic reads of slot contents);
  writes still need synchronization but the granularity can be
  per-slot (CAS on the slot's `edges` pointer) instead of per-shard.

## Design sketch

```c
typedef struct {
    _Atomic(mtpndd_edge_t *) edges;     /* NULL = empty, TOMBSTONE = deleted */
    mtpndd_node_t *node;                 /* stable once set */
    uint64_t cached_hash;                /* for verify-after-load */
} mtpndd_ot_slot_t;                      /* 24 bytes */

typedef struct {
    mtpndd_ot_slot_t *slots;             /* power-of-2 sized flat array */
    size_t mask;
    _Atomic size_t entry_count;
    size_t load_threshold;               /* resize at ~75% */
    /* concurrency: single rehash_mutex; reads are lock-free atomic loads */
    pthread_mutex_t rehash_mutex;
} mtpndd_ot_t;
```

- **Lookup** (lock-free):
  1. `h = edges->cached_hash; idx = h & mask;`
  2. Load `slots[idx].edges` relaxed. If NULL -> miss.
  3. If `slots[idx].cached_hash == h` and `nodetable_edges_equal(slot.edges, edges)`
     -> hit, return `slots[idx].node`.
  4. Else `idx = (idx + 1) & mask;` (linear probe) or `idx = (idx + step*step) & mask`
     (quadratic) and loop.
- **Insert** with CAS:
  1. Probe to find empty or tombstone.
  2. `atomic_compare_exchange_strong(&slot.edges, &expected, new_edges)`.
  3. On success write `slot.node` and `slot.cached_hash`. On failure
     reprobe (another thread claimed the slot).
- **Delete** (GC): write `TOMBSTONE` into `slot.edges`.
- **Rehash**: single-writer under `rehash_mutex`; copy live slots into
  a new larger array, replace `slots`/`mask` atomically, free old.

## Migration hazards

- `mtpndd_gc_mark_bdd_labels` currently walks `nodetable->buckets[i]`
  lists. Iteration API needs updating to walk the flat slot array.
- Rehash is currently per-field-table; under the new design it's still
  per-field but the implementation differs.
- Tombstones accumulate across GC cycles and eventually force rehash;
  need a tombstone-ratio threshold.
- Reference-count management on nodes doesn't change — the table only
  stores pointers, not node storage.

## Expected payoff

Hard to predict without prototyping. Upper bound is the combined
`find_node` + `mtpndd_mk` + `pthread_spin_lock` cost, ~27% of CPU at
N=12 W=6. If we can halve the lookup's memory-access cost we'd see
~10% end-to-end gain. Realistic target is **5-10%**.

## When to pick this up

Not urgent. Items that should be done first if coming back to perf:

- Algorithmic shrinks to `mtpndd_and_rec` itself (19% — tweak
  granularity, combine two-phase branches, etc).
- Look at `pthread_spin_lock` contention specifically (4%) — may be
  addressable without a full open-addressing rewrite.

Estimated effort for open-addressing rewrite: **1-2 weeks of focused
work** including:
- Prototype on a branch
- Concurrency audit (CAS ordering, ABA potential)
- GC integration
- Full benchmark regression
- Decision to merge / keep-chained
