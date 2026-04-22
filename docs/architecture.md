# Architecture of mtpndd-go

## Package layout

```
<repo root>
├── go.mod                         module github.com/Augists/mtpndd-go
├── node.go                        Node, edge, hashing, sort
├── nodetable.go                   NDD unique table + node slab allocator
├── ops.go                         And / Or / Not / Diff / Exist
├── opcache.go                     NDD operation cache (seqlock, inline slots)
├── engine.go                      Engine, Field, declare / generate lifecycle
├── satcount.go                    NDD sat-count (relies on disjoint edge labels)
├── mtpndd_test.go                 correctness tests including N=1..10 n-queens
├── internal/bdd/
│   ├── bdd.go                     Node, terminals
│   ├── ops.go                     And / Or / Not / Xor / Exist / SatCount
│   ├── table.go                   unique table + BDD node slab
│   ├── cache.go                   BDD operation cache (seqlock, inline slots)
│   ├── util.go                    uintptrOf, spawn heuristic
│   └── bdd_test.go
├── internal/work/
│   ├── work.go                    goroutine-based spawn/sync helper
│   └── work_test.go
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

Since the op cache stores strong result pointers and the unique table stores
strong canonical pointers, cached entries persist until `Reset()`. To bound
the op cache's pin set, every `nddCacheClearInterval = 2²² (≈4M)` puts the
whole cache is zeroed. The BDD cache has the same clear schedule.

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

`spawnPairThreshold = 4`. When `len(a.edges) × len(b.edges) ≥ 4`, the same-field
And / Or path fans out one goroutine per (ai, bj) pair. Below the threshold
everything runs inline.

BDD parallelism is disabled (`internal/bdd/util.go` subproblemBig returns
false). Early experiments showed that every BDD apply is small enough that
spawn overhead outweighs parallel gain; the NDD layer already supplies all
the concurrency the workload can use.

### Unique table sharding

64 shards, each protected by its own `sync.Mutex`. Shard index derived from
the cumulative edge hash. Concurrent `mk` calls on different shards proceed
in parallel; collisions within a shard serialize briefly.

Each shard is an open-addressing (linear-probe) hash table of
`(hash uint64, node *Node)` pairs rather than Go's builtin `map`. The
tables grow 2× when `count / len > 0.7`. On a hash match the probe
returns the candidate node **without** doing a secondary
`edgesEqual` / `nodeKey` check: the 64-bit hash is treated as the full
key (collision probability ~2⁻⁶⁴).

### Op cache slots

Flat arrays of `nddOpCacheSize = 2²⁰` (`opCacheSize = 2²⁰` in the BDD
package) inline `opSlot` structs. Per-slot concurrency is a **seqlock**.
Slots are 24 bytes — `(seq, fingerprint, result)` — where `fingerprint`
is a 64-bit hash of `(tag, aux, idA, idB)` used both as the bucket
index and as the full match key:

```go
type opSlot struct {
    seq atomic.Uint64   // even = stable, odd = writer active
    fp  uint64          // fingerprint of (tag, aux, idA, idB)
    res *Node           // strong reference
}
```

Reader:
1. Load `seq`. If odd, miss.
2. Read fields non-atomically.
3. Load `seq` again. If changed, miss.
4. Compare fields against query; on match return `res`.

Writer:
1. CAS `seq` from `k` (even) to `k+1` (odd). If CAS fails, the put is
   silently dropped. (Losing the race is acceptable — the caller recomputed
   anyway.)
2. Write fields.
3. Store `seq = k+2`.

### Node slab allocator

Nodes are bump-allocated out of pre-sized slab chunks instead of one
`new(Node)` per miss. Chunk size is `1 << 18 = 256 K` nodes; the chunk
directory is a fixed-capacity `[nddMaxChunks]atomic.Pointer[[]Node]`
array (1 G node upper bound).

The fast path is **lock-free**: one atomic `next.Add(1)` to claim an
index, one atomic load of the chunk pointer, one array index. The mutex
is only taken when a chunk pointer is nil (first allocation into that
chunk slot).

This keeps Go's GC scanner from visiting each node individually — it
sees one big `[]Node` slice per chunk.

Trade-off: nodes in a slab cannot be individually freed. Combined with
the strong-ref unique table, this means all memory is bound to
`Reset()`.

## Cache-operand validation via ID, not pointer

Every canonical Node carries a monotonically-increasing `id uint64`. The op
cache stores operand **IDs**, not pointers:

```go
type opSlot struct {
    ...
    idA uint64
    idB uint64
    res *Node
}
```

On lookup, `s.idA == a.id && s.idB == b.id` is enough to match. Why not
just compare pointers? Because a freed-then-reallocated node can reuse an
address, creating a false cache hit. The Go runtime's GC is non-moving, so
pointer comparison is stable while the object is alive — but the op cache
lives across many GC cycles and must defend against address reuse.

IDs cost two 64-bit compares and are never reused. See
[performance.md](performance.md) for the N=10 correctness bug that motivated
this design.

## sat-count

`mtpndd.SatCount(n, engine)` walks the NDD, multiplying the per-field label
satisfaction count by the child's sat count:

```
count(n, i) = Σ_e  bdd.satCount(e.label, field_i's vars)  × count(e.child, i+1)
```

The BDD `satCount` for a label is computed over `[bddVarBase, bddVarBase +
bitWidth)` by running `bdd.SatCount(label, bddVarBase + bitWidth)` and
dividing out the `2^bddVarBase` factor contributed by unused leading
variables. Fields that `n` does not branch on at its level contribute a
free `2^bitWidth`.

Correctness depends on **disjoint labels** at every NDD node, which is
maintained by the operations.
