# Architecture of mtpndd-go

## System layering

```
            ┌───────────────────────────────────────────────┐
            │   sre-ndd  (Java, application.config2spec)    │
            │                                               │
            │   org.ants.mtpndd.MTPNDD → JNI native methods │
            └────────────────┬──────────────────────────────┘
                             │  dlopen libmtpnddjni.so
                             ▼
            ┌───────────────────────────────────────────────┐
            │   jni/  (cgo c-shared, package main)          │
            │   • exports.go: ~60 Java_*_Native entrypoints │
            │   • main.go:    mapInitArgs, handle convert   │
            │   • pprof.go:   optional, -tags pprof         │
            │   Handles: jlong = uintptr(unsafe.Pointer(n)) │
            └────────────────┬──────────────────────────────┘
                             ▼
            ┌───────────────────────────────────────────────┐
            │   package mtpndd   (NDD layer)                │
            │   • engine.go, node.go, nodetable.go          │
            │   • ops.go:      And / Or / Not / Diff / ...  │
            │   • opcache.go:  NDD op cache (seqlock)       │
            │   • satcount.go: global open-addressed memo   │
            └────────────────┬──────────────────────────────┘
                             ▼
            ┌───────────────────────────────────────────────┐
            │   internal/bdd   (BDD layer)                  │
            │   • ops.go:    And/Or/Xor/Not/Diff/Exist/...  │
            │   • table.go:  unique table + slab            │
            │   • cache.go:  BDD op cache (seqlock)         │
            └────────────────┬──────────────────────────────┘
                             ▼
            ┌───────────────────────────────────────────────┐
            │   internal/work  (goroutine spawn/sync)       │
            └───────────────────────────────────────────────┘

Go-native callers (cmd/nqueens*) skip the top two boxes.
```

### Data-structure footprint at SRE fattree12 MF=3 scale

```
┌─ NDD layer ─────────────────────────────────┐   ┌─ BDD layer ─────────────────────────────┐
│                                             │   │                                         │
│ NDD slab: [2^15]atomic.Pointer[[]Node]      │   │ BDD slab: [2^15]atomic.Pointer[[]Node]  │
│   up to 8 G nodes, chunks of 2^18 entries   │   │  up to 8 G nodes, chunks of 2^18 entries│
│                                             │   │                                         │
│ NDD unique table: 64 shards of linear probe │   │ BDD unique table: 64 shards of linear   │
│   slot = (hash, nodeUintptr) × ~128 K       │   │   slot = (hash, nodeUintptr) × ~16 M    │
│                                             │   │                                         │
│ NDD op cache: 16 M × 24 B = 384 MB          │   │ BDD op cache: 16 M × 24 B = 384 MB      │
│   slot = (seq, fp, resUintptr)              │   │   slot = (seq, fp, resUintptr)          │
│                                             │   │                                         │
│ SatCount memo: 2^22 × 24 B = 96 MB          │   │                                         │
│   slot = (seq, fp, float64)                 │   │                                         │
└─────────────────────────────────────────────┘   └─────────────────────────────────────────┘

All three tables: uintptr result — GC doesn't scan them.
slab-rooted nodes kept alive via []Node backing; tables just point in.
```

## Package layout

```
<repo root>
├── go.mod                         module github.com/Augists/mtpndd-go
├── node.go                        Node, edge, hashing, sort
├── nodetable.go                   NDD unique table + node slab + edge arena
├── ops.go                         NDD And / Or / Not / Diff / Exist
├── opcache.go                     NDD op cache (seqlock, uintptr res)
├── engine.go                      Engine, Field, declare / generate; Config
├── satcount.go                    NDD SatCount w/ global open-addressed memo
├── mtpndd_test.go                 correctness tests including N=1..10 n-queens
├── internal/bdd/
│   ├── bdd.go                     Node, terminals, Mk
│   ├── ops.go                     And / Or / Xor / Not / Diff / Exist / SatCount
│   ├── table.go                   unique table + BDD node slab
│   ├── cache.go                   BDD op cache (seqlock, uintptr res)
│   ├── util.go                    uintptrOf, spawn heuristic
│   └── bdd_test.go
├── internal/work/
│   ├── work.go                    goroutine-based spawn/sync helper
│   └── work_test.go
├── jni/                           cgo c-shared build producing libmtpnddjni.so
│   ├── main.go                    cgo preamble, handle helpers, mapInitArgs
│   ├── exports.go                 ~60 //export Java_... native methods
│   ├── pprof.go                   (//go:build pprof) optional pprof server
│   ├── jni_helpers.h              JNIEnv vtable C shims
│   └── build.sh                   one-liner go build -buildmode=c-shared
└── cmd/
    ├── nqueens/main.go            single-N solver (CLI)
    └── nqueens-bench/main.go      sweep harness with -cpuprofile
```

## NDD data model

```go
type Node struct {
    fieldID uint32
    id      uint64     // monotonic; used by op caches to validate operands
    edges   []edge     // sorted by (uintptr(child), uintptr(label)) ascending
    hash    uint64     // cumulative edge hash, mixed with fieldID
}

type edge struct {
    child *Node        // NDD child at the next field
    label *bdd.Node    // BDD over this field's variables
}

var (
    True  = &Node{fieldID: terminalField, id: 1}
    False = &Node{fieldID: terminalField, id: 2}
)
```

Semantics match the C version: `NDD(n, σ) = ⋁ₑ (label_e(σ) ∧ NDD(child_e, σ))`.
Edge labels within a node's edge set are maintained as **disjoint BDDs**; the
sat-count relies on that invariant. Operations (`Or`, `Not`) restore
disjointness via residual-label tracking (see `ops.go`'s `orSameField`).

## Field system

Fields are declared in order, each with a bit width. `GenerateFields()`
finalizes the declaration and pre-materializes the positive and negative
literal NDD nodes for every bit. BDD variables are laid out contiguously:
field 0 gets variables `[0, w0)`, field 1 gets `[w0, w0+w1)`, etc.

```go
type Field struct {
    ID          uint32
    BitWidth    uint32
    bddVarBase  uint32
    varNodes    []*Node   // positive literal per bit
    notVarNodes []*Node   // negative literal per bit
}
```

## Lifetime model

### The core question

When a user drops an NDD root, when does the node (and its children, and its
edge labels, and transitively the whole DAG) actually get freed?

In C/Sylvan, this is driven by explicit `ref`/`deref` calls plus a GC hook.
In Go, tracing GC does it automatically — but only if nothing else pins the
node alive. The two things that could pin are:

1. **The unique table** (canonicalization map).
2. **The operation cache** (memoization of `And(a,b)` etc.).

### v1 decision: strong refs + session-scoped memory

Both the unique table and the op cache hold **strong `*Node` references**.
Any node ever created remains live until `mtpndd.Reset()` is called.
`Reset()` clears the unique tables, op caches, and BDD slab chunk pointers,
so subsequent allocations reuse fresh memory.

This is the opposite of what the v1 design doc proposed (weak pointers
everywhere). The change was forced by profiling: the Go runtime's
`weak.Pointer` bookkeeping (`runtime.getWeakHandle`,
`runtime.(*mspan).specialFindSplicePoint`) dominated CPU in the first
working version — ~40% of total time. A single `weak.Make` costs hundreds of
nanoseconds due to the sorted splice-point walk in the runtime's weak-handle
set. The op cache alone calls `weak.Make` ~5M times per N=12 n-queens run,
so the runtime bookkeeping became the bottleneck.

### Implication for long-running processes

Strong refs mean no automatic reclamation of unreachable intermediate nodes.
For a single benchmark run this is fine — memory grows monotonically to
~2.3 M nodes (≈ 180 MB) at peak for N=12 and then `Reset()` frees everything.
For a long-running service (SRE integration), the caller must call `Reset()`
between logical queries, or move to a weak-reference variant (see
`docs/roadmap.md`).

### Cache staleness

Since the op cache stores result pointers (as `uintptr` since v1.15 — see
below) and the unique table is the actual GC root for nodes, cached
entries stay valid for the entire session. Both caches support a periodic
`clearBDDCache` / `clearNDDCache` for hosts that want to cap logical
footprint, controlled by `CacheClearInterval`. At the default (`2²¹ ≈ 2 M`
puts) the clear triggers occasionally on Go-native workloads. The
JNI layer sets the interval to `1 << 62` (effectively off), since
`mtpndd-go`'s strong-ref design means periodic wipes drop otherwise-live
hits. See changelog v1.10 and v1.14 for the rationale and the atomic
cachePutCount gate.

## Concurrency primitives

### `internal/work`

A tiny spawn/sync helper on top of `go func()`:

```go
func Go(fn func()) *Future
func (*Future) Wait()
```

`Go` tries a non-blocking send on a global semaphore (`cap = GOMAXPROCS*2`).
On success it launches a goroutine; on backpressure it runs `fn` inline and
returns a pre-canned "already done" future. `Future`s come from and return
to a `sync.Pool`.

No work-stealing deque; Go's runtime schedules the goroutines. Lace's
per-push cost of ~100 ns isn't matched here — Go goroutine creation is
~0.5–1 µs — so spawn thresholds have to be conservative.

### NDD And/Or thresholds

Configurable via `Config.SpawnPairThreshold`. When
`len(a.edges) × len(b.edges) ≥ threshold`, the same-field And / Or path
fans out one goroutine per (ai, bj) pair. Below the threshold
everything runs inline.

- **Go-native default: 4.** Small cartesian products (SRE NDD edges are
  1–3 typically) inline; bigger NDD nodes (n-queens) parallelize.
- **JNI default: 1024.** `mapInitArgs` sets it to effectively disable
  in-Go spawning. Rationale: sre-ndd runs N Java threads each making
  independent JNI calls, so outer parallelism is already saturating
  the cores; goroutine spawn at this layer was 25 %+ of CPU in
  `runtime.futex` / `runtime.schedule` / `work.Go.func1` (v1.9
  changelog). Disabling it cut fattree08 MF=3 from 28.9 s to 15.8 s.

BDD parallelism is disabled (`internal/bdd/util.go` subproblemBig
returns false). Every BDD apply is small enough that spawn overhead
outweighs parallel gain; the NDD layer already supplies all the
concurrency the workload can use.

### Unique table sharding

64 shards, each protected by its own `sync.Mutex`. Shard index derived from
the cumulative edge hash. Concurrent `mk` calls on different shards proceed
in parallel; collisions within a shard serialize briefly.

Each shard is an open-addressing (linear-probe) hash table of
`(hash uint64, node uintptr)` pairs rather than Go's builtin `map`. The
tables grow 2× when `count * 10 > len * 5` (50 % load — v1.16; lower load
→ shorter probe chains → fewer cache-line misses at L3-spilling table
sizes). On a hash match the probe returns the candidate node **without**
doing a secondary `edgesEqual` / `nodeKey` check: the 64-bit hash is
treated as the full key (collision probability ~2⁻⁶⁴).

The slot stores `node` as `uintptr` (v1.17), not `*Node`, so GC doesn't
scan the slot array's pointer bitmap. Nodes are already rooted by the
slab; the slot's pointer was redundant for reachability. `Reset()` clears
all slots *before* dropping slab chunks, so the uintptr never dangles.

The NDD shard additionally owns an **edge arena** (v1.12) — a
4096-entry `[]edge` chunk used to back newly-interned nodes' edges
slice. Bump-allocated under the shard lock on insert; amortizes one
`runtime.mallocgc` per node down to one per ~4096 edges.

### Op cache slots

Flat arrays of `opSlot` structs sized by config (default `2^19`, SRE
sets `2^24`). Per-slot concurrency is a **seqlock**. Slots are 24 bytes
— `(seq, fingerprint, result)` — where `fingerprint` is a 64-bit hash
of `(tag, aux, idA, idB)` used both as the bucket index and as the full
match key:

```go
type opSlot struct {
    seq atomic.Uint64   // even = stable, odd = writer active
    fp  uint64          // fingerprint of (tag, aux, idA, idB)
    res uintptr         // v1.15: uintptr instead of *Node
                        //        (GC-invisible — nodes rooted by slab)
}
```

Reader:
1. Load `seq`. If odd, miss.
2. Read fields non-atomically.
3. Load `seq` again. If changed, miss.
4. Compare fingerprint against query; on match return `res` cast to `*Node`.

Writer:
1. CAS `seq` from `k` (even) to `k+1` (odd). If CAS fails, the put is
   silently dropped. (Losing the race is acceptable — the caller recomputed
   anyway.)
2. Write fields (storing result as `uintptr(unsafe.Pointer(n))`).
3. Store `seq = k+2`.
4. If `CacheClearInterval < 2^40`, advance `cachePutCount` (one atomic
   RMW) and maybe fire a clear. Above that threshold the atomic is
   skipped entirely — it's a cache-line-ping hotspot under contention
   and useless when clears are off (v1.14).

### Node slab allocator

Nodes are bump-allocated out of pre-sized slab chunks instead of one
`new(Node)` per miss. Chunk size is `1 << 18 = 256 K` nodes; the chunk
directory is a fixed-capacity `[nddMaxChunks]atomic.Pointer[[]Node]`
array (2^15 chunks → 8 G node upper bound per layer; v1.8 raised from
the earlier 2^12 = 1 G after fattree12 overflowed it).

The fast path is **lock-free**: one atomic `next.Add(1)` to claim an
index, one atomic load of the chunk pointer, one array index. The mutex
is only taken when a chunk pointer is nil (first allocation into that
chunk slot).

This keeps Go's GC scanner from visiting each node individually — it
sees one big `[]Node` slice per chunk.

Trade-off: nodes in a slab cannot be individually freed. Combined with
the strong-ref unique table, this means all memory is bound to
`Reset()`.

## Cache-operand validation via fingerprint, not pointer

Every canonical Node carries a monotonically-increasing `id uint64`. The op
cache stores a **64-bit fingerprint mixing `(tag, aux, idA, idB)`**, not
the operand IDs or pointers directly. Lookup does a single 64-bit compare
`s.fp == fp`.

```go
func bddFingerprint(tag opTag, idA, idB uint64, aux uint32) uint64 {
    h := idA*c1 ^ idB*c2 ^ ((uint64(aux) << 8) | uint64(tag))
    h ^= h >> 32; h *= c3; h ^= h >> 32
    return h
}
```

Why IDs and not pointers? Because a freed-then-reallocated node could
reuse an address and create a false cache hit. IDs are monotonic and
never reused while the session lives.

Why fingerprint and not `(idA, idB)` explicit fields? Because the
fingerprint doubles as the bucket index, the slot already has to store
it (or recompute it on mismatch), and matching on a single 64-bit hash
has the same collision probability (~2⁻⁶⁴) as comparing the raw IDs.
Trading a field save for one more 64-bit compare breaks even and keeps
the slot at 24 bytes.

See [performance.md](performance.md) for the N=10 correctness bug
(address reuse) that motivated moving away from pointer compare in the
first place.

## Global SatCount memo

`mtpndd.SatCount` memoises results keyed on `(node pointer, fieldIdx)`.
The memo is **process-global and persists across independent SatCount
calls** (they just invalidate at `Reset()`). Before v1.7 the memo was
per-call; sre-ndd invokes SatCount ~10⁶ times on heavily overlapping
subgraphs, and the global memo turned fattree08 MF=3 from 293 s → 29 s
(a 10× win, not replicated on the C backend — see the v1.17 sweep doc).

v1.13 replaced the original `[64]{RWMutex, map[key]float64}` sharding
with an open-addressed table: `2^22` slots × 24 B seqlock slots (96 MB).
64-bit fingerprint; collisions overwrite the victim (which just gets
recomputed on its next lookup). No mutex, no map, no GC work on the
memo entries. See changelog v1.13.

At fattree12 MF=3 scale C spends **289 s** on SatCount vs Go's **10 s**
— the 27× gap is almost entirely the memo. Excluding SatCount, Go is
only 1.4× faster than C on that workload.

### SatCount recurrence

```
count(n, i) = Σ_e  bdd.SatCount(e.label, field_i's vars) × count(e.child, i+1)
```

BDD `SatCount` for a label runs over `[bddVarBase, bddVarBase + bitWidth)`
and divides out the `2^bddVarBase` factor for unused leading variables.
Both 2^bitWidth and 2^bddVarBase are precomputed on `Field` (v1.11) so
the hot path is multiplication and cached `float64` lookups — no loops
over bit width.

Fields that `n` does not branch on at its level contribute a free
`2^bitWidth` factor (also precomputed).

Correctness depends on **disjoint labels** at every NDD node, which is
maintained by `orSameField` via residual-label tracking (v1.18 uses
`bdd.Diff` for this residual rather than `Not` + `And`).
