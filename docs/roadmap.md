# Roadmap — remaining performance gaps

## Status at end of v1.4 (measured against `mtpndd_nqueens_benchmark`)

| N  | C (s) | Go (s) | Go / C | Go advantage |
| -- | ----- | ------ | ------ | ------------ |
| 10 | 0.09  | 0.07   | 0.80×  | 20 %         |
| 11 | 0.36  | 0.30   | 0.86×  | 14 %         |
| 12 | 1.66  | 1.38   | 0.83×  | 17 %         |
| 13 | 9.43  | 7.40   | 0.78×  | 22 %         |

Go is 14–22 % faster than C across all tested N.

## Where the N=13 time goes (v1.3)

N=13 profile (6 workers, 7.5 s wall, 36 s CPU total):

```
 86 %  andSameField (cum)               ← most of the work
 37 %  mk (cum)
 23 %  nddCacheGet (cum)                ← seqlock reads on 24 MB cache
 17 %  nddCacheGet (flat)
 12 %  gcBgMarkWorker (cum)
  6 %  sync.Mutex.Lock (cum)            ← shard mutex in mk
```

`mk` flat 21 % is all memory access on the open-addressing probe:
`slot.node == nil` and `slot.hash == h` reads against a ~250 MB
working set. `nddCacheGet`'s 17 % flat is the seqlock read on the
24 MB op cache. Both are memory-bandwidth-limited — each random
access typically misses L3 and goes to main memory.

## Next planned work, prioritized

Current gap to C is small (≤ 21 %); Go already leads. Further wins are
deep cuts into memory-bandwidth territory. Listed in order of expected
impact on N ≥ 13.

### 1. Per-P op caches (the big lever for N ≥ 13)

The single 24 MB op cache is shared by all workers; each access can
evict another worker's hot line. A per-processor op cache (one cache
per `runtime.GOMAXPROCS` slot) trades some global hit rate for much
better cache locality. The rough plan:

- Allocate `GOMAXPROCS × 256K` slots (e.g., 6 × 256K = 1.5M total, same
  order as today). Each cache is a few MB and fits in L2 per core.
- Goroutines stick to their P via `runtime.LockOSThread` *only* at the
  work-pool boundary (not per-call), so the vast majority of work in one
  task runs on one P.
- Look up in the local cache first; on miss, optionally fall back to a
  smaller shared global cache, or just recompute.

Go doesn't give us direct P-index access, but `runtime_procPin` is
accessible via `runtime/internal/sys` (linkname) or can be approximated
using `runtime.GOMAXPROCS(0)` and a sharded index seeded from a
goroutine-local counter. Needs careful design.

Expected win at N=13: 30–50 % reduction in wall time if L2/L3 pressure
really is the bottleneck.

### 2. Two-phase AND pre-filter

The C version splits same-field AND into:
1. For every (a.edges[i], b.edges[j]) pair, compute the BDD AND of
   labels and drop pairs whose label is False.
2. Only recurse `And(child_i, child_j)` on survivors.

My Go v1.2 does the two in one loop, which spawns a goroutine even for
pairs that will later filter out. Separating the passes saves the
goroutine launch for dead pairs.

Expected win: modest on n-queens (most label intersections survive);
potentially larger on workloads with denser edge maps.

### 3. Lock-free unique table

`mk` still takes the shard mutex for every call. Uncontended cost is
~20 ns; across millions of calls that's 1–2 s per run. Going lock-free
means:

- Slot state machine: Empty → Writing → Valid.
- CAS from Empty to Writing to claim an insert slot.
- Readers skip Writing slots (spin or retry-probe).

Complex; only worth pursuing if per-P caches don't eliminate the issue.

### 4. Re-enable BDD-level parallelism selectively

Currently `internal/bdd.subproblemBig` returns false — all BDD work is
serial inside each NDD goroutine. At N ≥ 13 with smaller NDD edge
lists, the BDD recursion in each goroutine becomes a longer serial
critical path. Re-enabling BDD spawn for large subproblems (top var
close to 0, lots of children) might help.

Previous attempts at aggressive BDD spawning hurt performance at N=12
due to goroutine overhead; needs a tighter gate.

## Non-performance work queued

- **Multi-terminal leaves** (fractions, doubles) + Phase‑1 arithmetic
  (`plus`, `minus`, `times`, `divide`, `abstract_plus`). Intentionally
  out of scope for v1.
- **JNI / Java bridge**. The C version exposes an MTPNDD JNI layer
  consumed by `org.ants.mtpndd.*`. The Go port defers this to a future
  milestone; the plan is to expose handles through a pin table so the
  Java side can `Cleaner`-release them.
- **Weak-reference mode for long-running processes**. Strong-ref unique
  tables pin everything until `Reset()`. If the SRE integration needs
  continuous operation, add a config flag that turns the unique-table
  refs back into `weak.Pointer` at a measured performance cost.
