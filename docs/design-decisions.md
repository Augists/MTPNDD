# Design decisions — mtpndd-go

Things that went in, alternatives that were considered, and the reason
for the choice. Cross-referenced to the code and to
[performance.md](performance.md) where a number-driven decision was made.

## 1. Strong references everywhere, `Reset()` for lifecycle

**Decision.** Both unique tables and both op caches hold strong
`*Node` pointers. Node memory is reclaimed only via `mtpndd.Reset()`
(which also clears the slab chunk pointers). Op caches additionally
self-clear every `1 << 22` puts.

**Rejected alternative: `weak.Pointer[Node]` everywhere.** The v1 port
used weak pointers in both the unique table and the op cache, matching
the original plan's "let Go GC handle everything" goal. Profiling
showed `runtime.getOrAddWeakHandle` and
`runtime.(*mspan).specialFindSplicePoint` consuming ~40 % of total
CPU at N=12 — a single `weak.Make` walks a sorted list of weak
handles under a runtime-wide mutex. With ~5 M weak handles created
per run, the overhead was fatal.

**Trade-off.** Nodes live until the next `Reset()`. For benchmark
workloads this is bounded (~180 MB at N=12, ~450 MB at N=13) and
fine. For long-running processes a periodic `Reset()` between logical
queries is the intended escape hatch. A "weak mode" config flag could
be added later if SRE-integration ever needs continuous operation.

See `performance.md` → Step 3 ("Strong result in op cache") and
Step 4 ("Strong unique table").

## 2. Per-Node monotonic `id uint64` for op-cache validation

**Decision.** Every `Node` carries a monotonic `id` assigned at
creation. The op cache stores operand IDs (later replaced by a
64-bit fingerprint; see item 4); lookups validate by integer
comparison, not pointer or `weak.Value()`.

**Problem it solves.** When a canonical node is freed and its memory
is reused by a later allocation, a pointer-keyed cache entry can
silently alias the old operation's result onto a new operation that
happens to land on the same address. This produced a real bug at
N = 10 in v1.0 (sat count 1418 instead of 724). IDs are never
reused so the aliasing is impossible.

See `performance.md` → Step 1 (correctness bug) and Step 2 (ID-based
validation), and the memory note
`feedback_weak_cache_operands.md`.

## 3. 64-bit hash treated as full key (no `edgesEqual`)

**Decision.** Both the NDD unique table and the BDD unique table
probe by 64-bit hash only. On a hash match the candidate node is
returned **without** a secondary `edgesEqual` or `nodeKey` struct
compare.

**Collision probability.** `~2⁻⁶⁴` per pair of distinct keys, which
for ≤ 2³⁰ distinct operations in flight gives ≤ 2⁻³⁴ expected
incorrect results. The target workloads are far below that.

**Why it matters.** The secondary compare required dereferencing the
candidate node's `edges` slice, pulling in an extra cache line per
probe match. On N=13 this was 7 % of total CPU. See
`performance.md` → Step 12.

**Revertibility.** `edgesEqual` in `node.go` is retained as
`//nolint:unused` so a future config flag could re-enable the
secondary compare if a workload ever trips the collision rate.

## 4. Inline 24-byte op-cache slots + seqlock

**Decision.** The op cache is a flat `[2²⁰]opSlot` array of 24-byte
inline slots: `(seq atomic.Uint64, fp uint64, res *Node)`.
Concurrency is a seqlock — writers CAS `seq` from even to odd,
readers load `seq` before and after reading payload fields.

**Rejected alternative: `atomic.Pointer[opEntry]` with heap-allocated
entries.** Used in v1.1. Every `cachePut` allocated a fresh 40-byte
entry; ~5 M puts per run → ~200 MB of garbage, dominating GC. See
`performance.md` → Step 5.

**Rejected alternative: per-slot `sync.Mutex`.** Tried after
switching to inline slots; uncontended `Mutex.Lock` cost ~30 ns
showed up as ~17 % CPU. Seqlock drops that to ~5 ns per access.
See `performance.md` → Step 6.

**Slot size choice.** 24 bytes fits into half a cache line, so the
array of 2²⁰ slots is 24 MB — small enough to be L3-resident for
much of a run at typical working set sizes. Earlier iteration had 40
bytes per slot (tag/aux/idA/idB separately); shrinking to the
fingerprint-only design (item 3's sibling) cut the array to 24 MB.
See `performance.md` → Step 11.

**Losing-writer policy.** A writer whose CAS fails to move `seq`
from even to odd silently drops its put. The downstream effect is a
marginally higher cache miss rate and recompute; correctness is
unaffected.

## 5. Open-addressing unique table (not Go's `map`)

**Decision.** Each unique-table shard is a linear-probed
`[]shardSlot` with resize at 70 % load factor.

**Rejected alternative: `map[uint64][]*Node` per shard.** Used in
v1.1; at 9 M nodes the map no longer fits in L3 (~220 MB across
shards), and `mapaccess1_fast64` plus
`internal/runtime/maps.ctrlGroup.matchH2` together accounted for
30 %+ of CPU at N=13. Open addressing keeps the probe sequence in
one contiguous slice, halving the per-lookup cost. See
`performance.md` → Step 10.

**Shard count tuning.** 64 shards at `initialShardCap = 1024` (NDD)
and 512 (BDD) was compared to 256 shards at 512/256: the smaller
per-shard tables had more resize events and worse cache utilization
than expected. 64 × 1024 was the best point in the space we tested.

## 6. Node slab allocator, lock-free fast path

**Decision.** Nodes are bump-allocated from 256 K-node chunks; the
chunk directory is a fixed-capacity array of
`atomic.Pointer[[]Node]`. Allocation on the fast path is one
`atomic.Int64.Add` + one atomic pointer load.

**Rejected alternative: `new(Node)`.** At 2–9 M nodes per run, this
is ~2–9 M individual heap objects for the GC to track, which
inflated GC scan time. Slab allocation gives the GC one `[]Node`
per chunk (~36 objects for a full N=13 run). See `performance.md` →
Step 8.

**Rejected alternative: per-P sub-slabs with thread-local buffers.**
Considered when the single-mutex slab briefly looked like a
bottleneck. Profile showed the mutex was cold in practice — only
contested on chunk boundaries (every 256 K allocations). The
lock-free directory made even those fast. No per-P mechanism needed.

## 7. Stack-backed edge buffers in sequential ops

**Decision.** Every sequential boolean-op path in `ops.go` declares
`var stack [16]edge; edges := stack[:0]` and appends into it;
`mk` copies the backing array into a freshly sized node-owned slice
on miss. Escape analysis confirms the arrays stay on the stack for
≤ 16-edge lists (≥ 99 % of n-queens nodes).

**Rejected alternative: sync.Pool of edge slices.** A pool introduces
the "concurrent reader holding a pointer to a recycled entry" race —
the same class as the op-cache bug in item 2, but at the slice
level. Correct to design but fragile enough that the stack-buffer
approach won out once we realized `mk` could copy for free on miss.

**Rejected alternative: per-call `make([]edge, 0, total)`.** The
original design. At millions of calls per run this dominated
allocation. Switching to stack buffers eliminated ~9 M heap
allocations at N=13. See `performance.md` → Step 13.

**Why `[16]edge`?** Measured maximum edge count in n-queens NDD
nodes is well under 16. If a larger workload overflows, `append`
transparently grows to heap — correct but slower. If the overflow
becomes common, bump the constant.

## 8. Goroutine-based spawn/sync, no work-stealing deque

**Decision.** `internal/work.Go` uses `go func()` + a global
semaphore channel as a simple admission gate. No persistent worker
pool, no Chase–Lev deque.

**Rejected alternative: Chase–Lev deque with persistent workers.**
The original v1 plan. Deferred because profiling showed
`work.Go.func1` cumulative time = the actual work done inside
goroutines, not the creation cost. Go's own `go func()` is ~500 ns,
which is small relative to NDD boolean-op work per goroutine (tens
of microseconds per task in practice). Re-evaluate if a future
workload produces a lot of very tiny tasks.

**Rejected alternative: BDD-level parallel apply.** Tried with
aggressive spawn gates; every variant was slower than the serial
BDD baseline. Goroutine overhead > small-BDD-apply cost. Kept
`internal/bdd.subproblemBig` returning false. If BDD operands ever
grow large enough to matter, the function exists to flip back.

## 9. Fields and BDD variables at fixed offsets

**Decision.** `Engine.DeclareField` appends fields in order; each
field's BDD variables occupy a contiguous range
`[bddVarBase, bddVarBase + bitWidth)`. This is baked in at
`GenerateFields` time.

**Why it matters.** `SatCount` uses the fixed ranges to split NDD
sat-count across fields: the label's BDD sat-count is normalized by
`2^bddVarBase`. A dynamic variable reordering scheme would make
`SatCount` more complicated and is not needed for the target
workloads.

## 10. `True` and `False` as package-level sentinels

**Decision.** Both BDD and NDD expose two package-level `*Node`
sentinels for the terminals. Callers test with `==`.

**Trade-off.** Package-level singletons are cheaper than boxing a
`bool`-valued enum through a constructor, and compile-time `==`
checks are fast. The sentinel nodes have reserved IDs (1 for True,
2 for False) so they never collide with monotonically-assigned
runtime IDs (start at 100).

This also simplifies interoperability: a future `bdd.FromBool(b)` /
`mtpndd.FromBool(b)` would just return `True` or `False`.
