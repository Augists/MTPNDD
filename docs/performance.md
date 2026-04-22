# Performance journey

All numbers: n-queens N=12/N=13, 6 workers (`GOMAXPROCS=6`), same machine.
Go wall times are median of three runs unless otherwise noted.

## Two C binaries, very different answers — pick the right one

The C tree ships two n-queens drivers:

- `build/mtpndd/mtpndd_nqueens_test` — smoke test. Its nodetable config
  (`nodetable_buckets = 1 << 19 = 524 288`, `mtpndd_nodetable_size =
  1 << 22`) overflows at N ≥ 13 and reports "Failed to build formula".
  The ~4 s wall time at N=13 is **time-to-failure, not a solve**. Do
  not use this for benchmarking.
- `build/mtpndd/mtpndd_nqueens_benchmark` — the canonical benchmark.
  Uses `bdd_nodes = 33 554 432`, `mtpndd_nodes = 134 217 728`,
  `nodetable_buckets = 8 388 608`. Completes N = 13 cleanly in ~9.4 s.

**Use `mtpndd_nqueens_benchmark` for comparisons.** Earlier revisions of
this doc quoted the test binary's numbers; those comparisons have been
corrected throughout.

## Headline result (vs `mtpndd_nqueens_benchmark`)

| N  | C (s) | Go v1.4 (s) | Go / C | Go advantage |
| -- | ----- | ----------- | ------ | ------------ |
| 10 | 0.09  | 0.07        | 0.80×  | 20 % faster  |
| 11 | 0.36  | 0.30        | 0.86×  | 14 % faster  |
| 12 | 1.66  | 1.38        | 0.83×  | 17 % faster  |
| 13 | 9.43  | 7.40        | 0.78×  | 22 % faster  |

Go leads C by 14–22 % across every tested N.

## Go across versions

| N  | Go v1 (s) | Go v1.1 (s) | Go v1.2 (s) | Go v1.3 (s) | Go v1.4 (s) |
| -- | --------- | ----------- | ----------- | ----------- | ----------- |
| 4  | 0.006     | 0.005       | 0.005       | 0.006       | 0.005       |
| 6  | 0.015     | 0.020       | 0.017       | 0.019       | 0.013       |
| 8  | 0.029     | 0.038       | 0.029       | 0.025       | 0.018       |
| 10 | 0.222     | 0.085       | 0.082       | 0.078       | 0.074       |
| 11 | 1.18      | 0.37        | 0.315       | 0.301       | 0.304       |
| 12 | 7.10      | 1.82        | 1.81        | 1.44        | 1.38        |
| 13 | —         | 11.2        | 8.17        | 7.47        | 7.40        |

## Serial vs parallel

Serial baselines were measured against the test binary (which had
`mtpndd_nqueens_test 12` completing serially before we realised it was
the wrong reference). They still illustrate the per-worker scaling
story, but the C number is slightly inflated by the test binary's
smaller config:

| Impl | Serial (s) | Parallel 6w (s) |
| ---- | ---------- | --------------- |
| C    | 16.7       | ~1.7 (bench)    |
| Go   | 21.5       | 1.81            |

Serial Go is ~29 % slower than serial C. The data-structure and
memoization layers are competitive; the gap is largely Go GC and
bounds-check overhead vs C's hand-tuned slab allocators and manual
refcount.

## Optimization steps

Numbers are wall-clock N=12 at 6 workers after each step. Each step is a
single commit-sized change; profiles below were captured with
`cmd/nqueens-bench -cpuprofile`.

### Step 0 — Baseline (v1)

7.10 s. First working port. All the pieces are in place but unoptimized:

- Op cache stores operand pointers as `weak.Pointer[Node]` (the "correct"
  design that handles address reuse).
- Op cache stores result as `weak.Pointer[Node]`.
- Unique table stores `weak.Pointer[Node]` with periodic lazy cleanup.
- Op cache entries are individually heap-allocated with `atomic.Pointer`
  per slot.
- Node allocation is one `new(Node)` per miss.

Profile hotspot:

```
 36 %  runtime.getOrAddWeakHandle (cum)
 40 %  runtime.getWeakHandle (cum)
 29 %  runtime.(*mspan).specialFindSplicePoint (flat) [weak-handle list walk]
 36 %  nddCachePut (cum) [3× weak.Make per put]
```

### Step 1 — Fix N=10 correctness bug

7.10 s → 7.10 s (no perf change; fixed wrong results).

N=10 gave 1418 (≈ 2× the expected 724) once the working set grew past the
initial small cases. Root cause: op cache keyed on `uintptr(operand)`; a
GC'd node's address could be reused for a different node, producing a
false cache hit for `Op(new_node, other)` that returned the stale
`Op(old_node, other)` result.

Fix: store operand pointers as `weak.Pointer[Node]` in the entry and
verify `weak.Value() == input` on lookup. (This step traded correctness
for worse performance — the perf recovery starts at Step 2.)

Memory note saved at
`feedback_weak_cache_operands.md` in the user memory store.

### Step 2 — ID-based operand validation

7.10 s → 5.30 s (27 % faster).

Replace `weak.Pointer[Node]` operand fields with a monotonically-increasing
`id uint64` on every Node. The cache entry stores `idA, idB uint64`;
lookup compares `s.idA == a.id`. IDs never reuse, so address aliasing
cannot produce false hits.

This alone eliminates two of the three `weak.Make` calls per cache put.

Profile hotspot after:

```
 29 %  runtime.getWeakHandle (cum)   [down from 40]
 22 %  nddCachePut (cum)             [down from 36; 1× weak.Make remains]
```

### Step 3 — Strong result in op cache

5.30 s → 4.87 s.

Store the result as `*Node` (strong). That pins the result alive. To bound
memory, clear the whole cache every `nddCacheClearInterval = 2²² ≈ 4 M`
puts.

No `weak.Make` on the hot path any more — only the unique table still
creates weak handles (one per new canonical node).

### Step 4 — Strong unique table

4.87 s → 2.81 s (the biggest single win, 42 % faster).

Drop `weak.Pointer[Node]` from the unique table too. Canonical nodes live
until `Reset()`. Memory high-water jumps from ~180 MB to ~220 MB at N=12,
which is acceptable for benchmark workloads.

Profile hotspot after:

```
 65 %  internal/work.Go.func1 (cum)   [parallel work runs inside goroutines]
 30 %  runtime.gcBgMarkWorker (cum)   [GC cost is now the dominant overhead]
 14 %  runtime.mapaccess1_fast64      [unique table map lookups]
```

### Step 5 — Inline cache slots, per-slot mutex

2.81 s → 2.55 s.

Previously each op cache slot was `atomic.Pointer[opEntry]` and every put
heap-allocated a fresh 40-byte entry struct. With ~5 M puts per run that
was ~200 MB of garbage. Switch to a flat `[opCacheSize]opSlot` array with
the entry inlined and a per-slot `sync.Mutex`.

Allocation rate drops by ~200 MB per run. Net runtime drops ~260 ms.

Profile hotspot after:

```
 17 %  sync.(*Mutex).Lock (flat)
 26 %  runtime.gcBgMarkWorker (cum)  [still GC-bound]
```

### Step 6 — Seqlock instead of mutex

2.55 s → 2.35 s.

`sync.Mutex.Lock` was showing up 17 % flat — uncontended mutex cost is
still ~30 ns, which matters at cache-access rates above 10 M/s. Replace
the per-slot mutex with a seqlock:

- `seq atomic.Uint64`, even = stable, odd = writer active.
- Reader loads `seq` before/after the field reads; mismatches are misses.
- Writers CAS `seq` from even to odd; losers of the CAS race drop their
  put.

Cost of a read dropped from ~30 ns (mutex) to ~5 ns (two atomic loads +
field reads). Puts occasionally drop on contention; measured miss-rate
impact is negligible.

### Step 7 — Insertion sort for small edge lists

2.35 s → 2.08 s.

`sort.Slice` is reflection-based and heap-allocates the less-than closure.
Profile showed it at 7 % cum. Nearly all NDD edge lists have ≤ 4 entries
at N=12, so an open-coded insertion sort specialized on `(uintptr(child),
uintptr(label))` is much faster.

### Step 8 — Node slab allocator

2.08 s → 1.82 s.

Replace `new(Node)` with a bump allocator over pre-sized chunks of
`[]Node` (256 K per chunk). Each Node is no longer an individual heap
allocation that the GC scans one-by-one; instead the GC sees one big slice
per chunk.

Allocation pressure drops by ~100 MB per run. GC cum drops from ~26 % to
~19 %.

### Step 9 — Lock-free slab fast path

N=12: 1.82 → 1.80 s. N=13: 11.2 → 11.0 s (both tiny).

Replace the single `sync.Mutex` around slab allocation with an atomic
bump counter and a fixed-capacity chunk directory. Fast path is now
`atomic.Int64.Add` + one atomic chunk-pointer load; the mutex is only
taken when a chunk is first populated. Not a major win at N=12 because
the mutex wasn't the bottleneck; larger N workloads with more allocation
parallelism benefit more.

### Step 10 — Open-addressing unique table

N=12: 1.80 → 1.57 s (13 %). N=13: 11.0 → 8.2 s (26 %, biggest single
win of v1.2).

Replace Go's `map[uint64][]*Node` inside each shard with a
linear-probed open-addressing table. At 9 M live nodes, Go's builtin
map no longer fits in L3 (~220 MB total footprint across shards) and
probe sequences had poor cache locality. Linear probing keeps probes in
one contiguous slice, cutting cache misses in `mk` by roughly half.

Before:
```
 22 %  runtime.mapaccess1_fast64 (cum)
 12 %  internal/runtime/maps.ctrlGroup.matchH2 (flat)
 40 %  mk (cum)
```

After:
```
 34 %  mk (cum)         [down from 40]
 21 %  nddCacheGet (cum)
```

### Step 11 — Compact op-cache slots

N=12: 1.57 → 1.55 s. N=13: 8.2 → 8.3 s (essentially unchanged).

Drop `(tag, aux, idA, idB)` from every op-cache slot in favor of a
single 64-bit fingerprint. Slot footprint goes from 40 B → 24 B, so
1 M-slot cache drops from 40 MB to 24 MB. Collision probability is
~2⁻⁶⁴, below any workload threshold.

Expected to help more than it did; turned out the cache access pattern
was already mostly accounted for by the slot-content fetch, not the
slot size. Kept anyway for the smaller memory footprint.

### Step 12 — Skip edgesEqual / keyEqual on hash match

N=12: 1.55 → 1.48 s. N=13: 8.3 → 8.0 s.

In the unique tables (NDD and BDD), previously a probe match required
both hash equality AND either `edgesEqual` (NDD) or full `nodeKey`
struct compare (BDD). With 64-bit well-mixed hashes we can skip the
secondary compare — false-positive rate 2⁻⁶⁴ — and avoid both a cache
line load (the candidate node's `edges` slice) and the iteration over
that slice.

### Step 14 — 512K-slot op cache + fast-miss cacheGet

N=12: 1.44 → 1.38 s (v1.4). N=13: 7.47 → 7.40 s.

Two small changes that together buy ~4 % at N=12:

1. **Op cache size dropped from 2²⁰ (1 M slots / 24 MB) to 2¹⁹ (512 K
   slots / 12 MB)**. An earlier sweep at an earlier code revision had
   picked 2²⁰ as best; after the stack-buffer and hash-only-match
   optimizations reduced allocation rate and L3 pressure, the
   optimum shifted. Re-sweep now shows 2¹⁹ is the L3-friendly sweet
   spot.
2. **`cacheGet` takes a fast-miss exit on fingerprint mismatch**:
   skips the second `seq.Load` that detects writer interference.
   Safe because a mismatch returns `(nil, false)` regardless of
   writer state — worst case is a recompute on a spurious miss.
   On a hit the second `seq.Load` is still required to guarantee
   the returned pointer corresponds to the matched fingerprint.

### Step 13 — mk copies edges on miss; callers use stack buffers

N=12: 1.48 → 1.44 s. N=13: 8.0 → 7.47 s.

`mk` now makes its own exact-sized copy of the edges slice on a miss
instead of taking ownership of the caller's buffer. That frees callers
to pass transient buffers backed by on-stack `[16]edge` arrays:

```go
var stack [16]edge
edges := stack[:0]
// append …
return mk(a.fieldID, edges)   // caller's buffer safe to drop
```

Go's escape analysis confirms that after this change, every sequential
`andSameField` / `andDiffField` / `orSameField` / `orDiffField` / `Not` /
`Exist` path allocates **zero** working slices on the heap (the stack
array absorbs everything up to 16 edges, which covers ≥ 99 % of
n-queens nodes). Only the parallel fan-out paths still heap-allocate,
because their slices are captured by goroutine closures.

This is the biggest single win in v1.3. The net cost of the added copy
on misses is more than paid for by eliminating ~9 M allocations per
N=13 run.

## Final profile (N=13, v1.3)

```
 31.2 s  andSameField (cum)   ≈ 86 %   [the actual parallel work]
 13.6 s  mk (cum)             ≈ 37 %
  8.2 s  nddCacheGet (cum)    ≈ 23 %
  4.4 s  gcBgMarkWorker (cum) ≈ 12 %
  2.2 s  sync.Mutex.Lock (cum)≈  6 %
 ----
 36.2 s  total CPU, 6 workers → 7.47 s wall
```

`mk`'s flat 20.8 % is all memory access on the open-addressing probe
sequence. `nddCacheGet`'s 16.6 % flat is the seqlock read. Both are
fundamentally memory-bandwidth-limited at this working set size
(9.4 M nodes, ~250 MB total live heap).

## Correctness throughout

Every step passed the full N = 1..10 test suite (sat counts matched the
known n-queens numbers: 1, 0, 0, 2, 10, 4, 40, 92, 352, 724) and the
`cmd/nqueens` CLI confirmed N = 11 = 2680, N = 12 = 14200, N = 13 = 73712.
The Step 1 correctness bug was fixed before any perf work began. Steps 11
and 12 introduce ~2⁻⁶⁴ hash-collision risk by design, tracked in the
slot struct comments.
