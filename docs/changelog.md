# Changelog — mtpndd-go

Dates reflect commits on the `feature/go` branch.

## 2026-04-23 — fattree12 MF=3 full Go-vs-C comparison

Measured against C backend (mtpndd-c feature/c `libmtpnddjni.so.c-backup`,
Sylvan+Lace) — both backends complete the workload on the 31 GB host,
single run each at `-Xmx32768m` w=4:

| metric | Go v1.15 | C | C / Go |
| --- | --- | --- | --- |
| **Wall** | **224.1 s** | 565.8 s | **2.52×** |
| MTPNDD TOTAL (Java timer) | 190.8 s | 539.7 s | 2.83× |
| And (33.3 M calls) | 61.7 s | 109.7 s | 1.78× |
| Or (6.1 M calls) | 78.3 s | 84.9 s | 1.08× |
| Not (5.5 M calls) | 33.8 s | 53.4 s | 1.58× |
| **SatCount (5.3 M calls)** | **10.5 s** | **289.3 s** | **27.6×** |
| ref / deref | ~6 s | — | — |

Go is **2.52× faster wall-time** on this workload. The lion's share of
the win is SatCount: C recomputes the full DAG on every call, Go
reuses the global memo introduced in v1.7. If SatCount were equal the
overall ratio would be ~1.39× (Go still faster, but much less dramatic).

Sanity: op counts are identical (same algorithm, same inputs), so the
speedups reflect implementation differences and not different work.

## 2026-04-23 — v1.15: store op-cache result as uintptr (GC-invisible)

Op-cache slots held `res *Node`. With SRE's 16 M-slot BDD cache (+ a
similar NDD cache), GC was scanning ~134 M byte of pointer fields per
cycle and applying write barriers on every cachePut. Profile on
fattree12 MF=3 w=4 showed:
- GC-related cum: `scanObject 4.78 s + findObject 1.27 s + ...
  = 9.3 s / 38.2 s = **24 %** of CPU`
- `cachePut` flat **2.25 s** (5.9 %) — most of it write-barrier code.

Since nodes are pinned by the unique table for the life of a session,
the op cache doesn't need GC to trace it. Change `res` to `uintptr`;
reads do `(*Node)(unsafe.Pointer(res))`, writes do
`uintptr(unsafe.Pointer(res))`. Safe because:
- Reader's returned `*Node` is always live — unique table still holds
  the strong ref.
- No dangling uintptr: `Reset()` clears every slot *before* dropping
  the slab chunks that back the nodes.

### Impact (fattree12 MF=3 w=4)

| metric | v1.14 | v1.15 | delta |
|---|---|---|---|
| Wall | 229.2 s | **224.1 s** | −2.2 % |
| MTPNDD TOTAL | 194.4 s | 190.8 s | −1.9 % |
| Or | 80.6 s | 78.3 s | −2.9 % |
| cachePut flat (pprof) | 2.25 s | 1.33 s | −41 % |
| GC cum (pprof) | 9.3 s | 7.4 s | −20 % |

`cachePut` drops ~40 % because storing a uintptr skips the GC write
barrier entirely; scanObject/findObject drop because the op cache
is no longer in the pointer-bitmap sweep.

### Impact (fattree08 MF=3 w=4)

14.68 s → 14.46 s (~1.5 %). Smaller cache (2 M slots, 48 MB) meant
lower GC load to begin with.

### Impact (n-queens N=12)

Unchanged — cache is 512 K slots (12 MB); GC was already cheap.

## 2026-04-23 — v1.14: skip cache-put counter atomic when clears are off

Found on the fattree12 MF=3 profile (first time this workload was
deliberately profiled — it runs fine on v1.x, the earlier SIGBUS seen
in a scratch hs_err was on a much older .so, not a real mtpndd-go
limit). Under heavy concurrency the cache-put
code path was spending **880 ms flat / 2.3 % of CPU** on a single line:

```go
if c := bddCachePutCount.Add(1); c%bddCacheClearInterval == 0 { ... }
```

The `bddCachePutCount.Add(1)` is an atomic RMW on a shared global. With
4 Go workers × 45 M cachePut calls, the counter cache line ping-pongs
between cores relentlessly. And for JNI callers the interval is already
`1 << 62` (v1.10 disabled periodic clears), so the increment is doing
zero useful work.

Gate both BDD and NDD cachePut's counter bump on
`interval < (1 << 40)`. When the interval is effectively disabled the
atomic and modulo are skipped entirely; when a caller does want periodic
clears (default 2^21) the behavior is unchanged.

### Impact

- **fattree12 MF=3 w=4**: wall 240.4 s → **229.2 s (−4.7 %)**, Java-side
  MTPNDD TOTAL 205.9 s → 194.4 s (−5.6 %). Per-op: And −6 %, Or −6 %,
  Not −6 %.
- **fattree08 MF=3 w=4**: 14.66 s → 14.68 s (noise; atomic contention
  wasn't the bottleneck at this scale).
- **n-queens N=12**: 1.60 s (unchanged — default interval still active
  for Go-native callers).

Lesson: atomic RMW on a global "just for a threshold check" is free at
low concurrency and devastating at high concurrency. If the write is
dead-weight on the hot path, skip it.

## 2026-04-23 — v1.13: open-addressed SatCount memo (replace sharded map)

Small SRE win: ~1 % (same-session A/B, 14.80 s v1.12 → 14.66 s open-
addressed; 5 and 10 run medians on fattree08 MF=3 w=4).

The memo was a `[64]{RWMutex, map[satCountKey]float64}`. `mapaccess2`
flat was 0.27 s (2.2 %), plus RLock/RUnlock per lookup and GC work
on the growing maps (`scanObject` / `findObject` tied closely to map
entry count).

Replaced with a single 2²²-slot open-addressed table (24 B/slot
seqlock layout, 96 MB total). Fingerprint = mixed hash of
`(nodePtr >> 3)` and `fieldIdx`. Readers: one atomic seq load,
fingerprint compare, float64 load, seq re-load — zero mutex work.
Collisions overwrite the victim, which is simply recomputed on
its next lookup. 64-bit fingerprint means false hits are ~2⁻⁶⁴.

Peak memo size on fattree08 MF=3 is 1–2 M entries, so 4 M slots
keeps load factor ≤ 50 % and collision rate negligible in the
steady state.

Memory cost vs. the old map is actually higher (map grew to ~50 MB;
now fixed 96 MB), but the machine has 31 GB and we'd rather trade
RAM for consistent low-latency probes.

## 2026-04-23 — v1.12: per-shard edge arena in NDD mk

Small win on sre-ndd fattree08 MF=3 w=4: ~1.3 % (same-session A/B,
14.88 s baseline → 14.70 s arena; 5 and 10 run medians).

`mk` allocated a fresh `[]edge` per newly-interned node
(`make([]edge, len(edges))` then `copy`). 130 ms cum in pprof, plus
general GC pressure (scanObject / mallocgcSmallScanNoHeader
~5–7 % combined).

Each `nddShard` now owns an `edgeChunk []edge` and an offset. The
miss-path bump-allocates out of the current chunk under the existing
shard lock; on chunk exhaustion it allocates a new 4096-edge chunk.
Nodes are never freed except on `Reset()` so the arena grows
monotonically, matching the node slab's lifetime policy.

Amortises one runtime.mallocgc per node down to one per ~4096 edges.
No API change. Reset now clears `edgeChunk`/`edgeOff` alongside the
slot arrays.

## 2026-04-23 — v1.11: precompute 2^bitWidth / 2^bddVarBase on Field

Small but clean SRE win: fattree08 MF=3 w=4 drops 15.03 → 14.59 s
(5-run medians in the same session, ~3 %).

`pow2int` was a `for range n { p *= 2 }` scalar loop called on every
satCountRec invocation — 170 ms flat (1.4 %) in the pprof profile,
plus the labelCount path had a division by `pow2int(field.bddVarBase)`
per edge.

Two changes on `Field`:
- Cache `pow2BitWidth = math.Ldexp(1, BitWidth)` at DeclareField time.
- Cache `pow2Base = math.Ldexp(1, bddVarBase)` too; satCountRec now
  multiplies by the reciprocal rather than dividing per edge.

Computed once at field creation, then constant for the life of the
session. No API change; `pow2int` helper deleted.

## 2026-04-23 — v1.10: disable periodic op-cache wipes for JNI callers

Small but real SRE win on fattree08 MF=3 w=4: 15.42 s → 15.03 s
(~2.5 %, 5-run medians in the same session). Variance also tightened
(5-run range 0.29 s vs baseline 0.67 s).

Root cause: the JNI init path mapped the Java-side opCache argument
(2^19 slots) to `CacheClearInterval = opCache * 4 = 2^21` puts.
sre-ndd fattree08 MF=3 executes ~3.5 M And/Or ops per run, so the
interval triggers mid-session and `clearBDDCache` wipes every slot,
forcing a cold rebuild.

With the strong-ref unique table, cached `*Node` entries stay valid
for the entire session (until `Reset`), so the wipe adds no safety.
It's pure throw-away of live hits. For JNI callers the wipe is now
effectively disabled (`CacheClearInterval = 1 << 62`). Go-native
callers (nqueens CLI) keep the default 2^21 — smaller op counts
there mean the interval is never hit anyway, but the default stays
conservative.

### SRE impact (bgp_fattree08 MF=3 w=4, same-session A/B)

| variant             | median    | 5-run range |
| ------------------- | --------- | ----------- |
| baseline (with wipe)| 15.42 s   | 15.14–15.81 |
| no periodic wipe    | **15.03** | 14.78–15.07 |

fattree12 MF=1 w=4 is flat (20.00 s vs prior 19.79 s — within noise);
the cache fits the op count there without needing to survive a wipe.

## 2026-04-23 — failed experiment: pack opSlot 24 → 16 B (seq in fp LSB)

Not merged — recorded here so it isn't retried.

Hypothesis: `cacheGet` line `seq1 & 1 != 0` was 1.61 s / 2.10 s in the
pprof profile — cache-miss latency fetching a 24-byte `opSlot`. Merging
the seqlock lock bit into the fingerprint's LSB (bit 0 = 1 means
writer in progress; valid fingerprints always even) would:
1. Shrink slot 24 → 16 B, so the 2^19-slot cache fits 8 MB (was 12 MB).
2. Align every slot to a 64 B line (24-byte slots straddle lines).
3. Remove one atomic load in the fast-miss path.

Result: **3 % slower** in a same-session A/B — 15.88 s packed median
vs 15.42 s baseline median (7 and 5 runs on fattree08 MF=3 w=4).

Why it didn't win: 12 MB already fits comfortably in L3 on this 6-core
host (≥ 12 MB L3), so shrinking the working set didn't translate to
fewer DRAM misses. The line-straddle savings were offset by extra
atomic ops — `fp.Load`/`fp.Store` now atomic, where the old `fp` was
a plain uint64 protected by the adjacent seq word. pprof's "1.61 s on
the seq1 check" overstates the fixable cost: most of it is the L1
fill for the slot, which must happen regardless of layout.

Lesson: when the working set already fits in L3, shrinking it further
is zero-value. Aim instead for reducing the *number* of slot accesses
(smarter hashing, better locality, higher hit rate).

## 2026-04-23 — failed experiment: move BDD unique-table hash onto Node

Not merged.

Hypothesis: `bdd.intern` profiled at 11.6 % flat with `slot.node == nil`
at 520 ms. Slot is `{hash uint64; node *Node}` = 16 B → 4 slots per
64 B line. Shrinking to just `*Node` (hash moved onto the Node itself)
would put 8 slots per line, halving probe cache-line fetches.

Result: **10 % slower** — 16.62 s median vs 15.13 s baseline, 3 runs
each on fattree08 MF=3 w=4.

Why it regressed: the old inline `slot.hash` let collision probes be
rejected without dereferencing the Node pointer. Moving `hash` onto
Node forces a pointer chase on every probe step. The chase cost
exceeded the cache-density win — `*Node` targets live in the slab,
different cache lines from the slot array, so each probe now touches
two lines instead of one.

Lesson: for probe-heavy hash tables, inline *everything the probe loop
reads*. Only indirect what's returned on a hit.

## 2026-04-23 — v1.9: disable in-Go goroutine spawning for JNI callers

Huge SRE win — fattree08 MF=3 w=4 went from 28.9 s to 15.7 s
(1.84× faster), making Go **2.06× faster than C** on that workload.

Root cause, exposed by pprof of the running JNI: 25 %+ of Go CPU was
in goroutine machinery (`runtime.futex`, `runtime.schedule`,
`work.Go.func1`). Every same-field And/Or with cartesian product ≥ 4
spawned sub-goroutines. But sre-ndd runs N Java threads each making
independent JNI calls, so the outer parallelism already saturated
cores; the inner goroutines just over-subscribed the scheduler and
wasted CPU on scheduling overhead.

Fix: `mapInitArgs` in the JNI bridge now sets
`cfg.SpawnPairThreshold = 1024`, effectively turning off in-Go
spawning for JNI callers. Go-native callers (the `cmd/nqueens-bench`
CLI) keep the default of 4 and still benefit from intra-op
parallelism — nqueens N=12 stays at ~1.5 s.

### SRE impact (bgp_fattree08 MF=3, 3-run median)

| workers | Go before | Go after | C     | Go after vs C |
| ------- | --------- | -------- | ----- | ------------- |
| w=1     | 37.14 s   | 16.28 s  | 35.78 | 2.20× faster  |
| w=2     | 29.01 s   | 15.56 s  | 33.05 | 2.12× faster  |
| w=4     | 28.91 s   | 15.13 s  | 32.40 | 2.14× faster  |
| w=6     | 28.93 s   | 15.25 s  | 32.11 | 2.10× faster  |

### Full sweep against C (at w=4)

| workload        | Go     | C     | Go / C |
| --------------- | ------ | ----- | ------ |
| ft04 MF=1       | 0.48 s | 0.49  | parity |
| ft04 MF=2       | 0.37 s | 0.36  | parity |
| ft04 MF=3       | 0.37 s | 0.36  | parity |
| ft08 MF=1       | 2.52 s | 3.38  | 0.75×  |
| ft08 MF=2       | 5.71 s | 10.89 | 0.52×  |
| ft08 MF=3       | 15.13 s| 32.40 | 0.47×  |
| ft12 MF=1       | 19.79 s| 26.47 | 0.75×  |

## 2026-04-23 — v1.8: MaxChunks 2^12 → 2^15 (raised slab ceiling)

Bumped the slab chunk-directory cap in both the NDD (`nodetable.go`)
and BDD (`internal/bdd/table.go`) layers from 2^12 (~1 G-node ceiling)
to 2^15 (~8 G-node ceiling). sre-ndd `bgp_fattree08` workloads
cumulatively allocate well over 1 G BDD nodes per session and hit
the earlier cap; 2^15 gives headroom for all measured workloads
except `fattree12 MF=3`, which is not feasible without per-node GC
(see the v2.0 roadmap note below).

Directory memory cost is negligible: 2^15 × 8 B = 256 KB per layer
whether used or not.

## 2026-04-23 — v1.7: global SatCount memoisation (10× on SRE)

The un-memoised SatCount drove 90 % of wall time on sre-ndd
`bgp_fattree08 MF=3 w=4` (265 s satCount / 293 s total). Root
cause: each call re-walked the full NDD + BDD DAG, and SRE-NDD
invokes it ~10^6 times on heavily overlapping subgraphs.

Two-level memo fixes it:
- NDD SatCount: process-global `map[(node, fieldIdx)] float64`.
- BDD SatCount: process-global `map[(node, nvars)] float64`.

Nodes are immutable for the life of a session so both caches stay
valid until `mtpndd.Reset()` / `bdd.Reset()` invalidate them.

Impact on sre-ndd `bgp_fattree08 MF=3 w=4`:

| stage                    | satCount time | total wall |
| ------------------------ | ------------- | ---------- |
| before any memo          | 265.68 s      | 292.80 s   |
| per-call memo only       |  74.59 s      | 102.27 s   |
| global memo (this fix)   |   1.15 s      |  28.90 s   |

C reference on the same machine: 32.9 s wall. With the global memo,
Go leads C by ~12 % on this SRE workload.

## 2026-04-23 — v1.6: Go-backed libmtpnddjni.so (SRE integration)

`mtpndd-go` now ships a cgo `c-shared` build that produces a
`libmtpnddjni.so` binary-compatible with mtpndd-c's. The same
`org.ants.mtpndd` jar (as shipped by mtpndd-c) dlopens either .so
and hits Go or C underneath.

Structure:
- `jni/main.go` — cgo preamble, lifecycle, handle helpers.
- `jni/exports.go` — ~60 `//export Java_..._Native` functions.
- `jni/jni_helpers.h` — small C shims for JNIEnv vtable calls
  (FindClass / ThrowNew / NewLongArray / NewObject).
- `jni/build.sh` — one-line invocation of `go build -buildmode=c-shared`.

Handle representation is `uintptr(unsafe.Pointer(*mtpndd.Node))` —
zero indirection, stable for the life of a session (Go's GC is
non-moving and unique tables pin nodes). Ref/deref are no-ops in
v1.x (the strong-ref unique table makes them unnecessary).

Unsupported surface (throws `MTPNDDException`):
- Multi-terminal leaves (`makeFraction`, `makeDouble`,
  `isFractionLeaf`, `getNumer`, etc.).
- Phase-1 arithmetic (`plus`, `minus`, `times`, `divide`,
  `abstractPlus`).

Validated on sre-ndd `bgp_fattree04` (27k And / 4k satCount) and
`bgp_fattree08 MF=3 w=4` (3.5M And / 1M satCount).

## 2026-04-23 — v1.5: runtime-configurable tuning knobs

Replaces hardcoded sizing constants with a `Config` struct threaded
through `mtpndd.Init` / `NewEngineWithConfig`. Required for
SRE-scale workloads (bgp_fattree08+) which want larger op caches
and slab chunks than the n-queens defaults.

Configurable per-layer (NDD + BDD each): `OpCacheSize`,
`CacheClearInterval`, `ShardCount`, `InitialShardCap`, `SlabChunkSize`.
Plus global `SpawnPairThreshold`. All powers of two.

Default config is unchanged from v1.4. Cost of the compile-time-
constant → runtime-variable mask transition on n-queens N=12:
1.38 s → 1.55 s (~12 % regression, acceptable for flexibility).

## v2.0 roadmap note (extreme-scale)

`sre-ndd bgp_fattree12 MF=3` overruns mtpndd-go's slab chunk directory
(>8 G cumulative BDD allocations per session). On the 31 GB test
machine the C reference also fails at this workload — SIGABRT, no
completion — so MF=3 is an OOM for both backends on current hardware
rather than a mtpndd-go-specific limit. `fattree12 MF=1` completes
cleanly on both (Go 26.2 s, C 28.6 s, Go ~8 % faster).

That said, on a larger host the Go "pin until Reset" policy would
eventually bite before C's ref-counted slab-slot reuse does.
Addressing requires:

1. Pin table at the JNI boundary; honour Java `refNative` /
   `derefNative` so Java handles keep their node alive until Java
   explicitly releases.
2. Weak-reference BDD unique table + `runtime.AddCleanup` so Go GC
   reclaims unreachable nodes and their slab slots.
3. Per-chunk free lists so vacated slab slots get reused.

Deferred to v2.0; for v1.x, valid workloads are `fattree04`,
`fattree08`, and `fattree12 MF=1`, i.e. anything that stays under
~4 G cumulative BDD allocations per session.

## 2026-04-23 — v1.4: 512K-slot op cache + fast-miss cacheGet

| N  | C (s) | Go v1.4 (s) | Go / C | Advantage |
| -- | ----- | ----------- | ------ | --------- |
| 10 | 0.09  | 0.07        | 0.80×  | 20 %      |
| 11 | 0.36  | 0.30        | 0.86×  | 14 %      |
| 12 | 1.66  | 1.38        | 0.83×  | 17 %      |
| 13 | 9.43  | 7.40        | 0.78×  | 22 %      |

### Changed

- **Op cache size dropped from 2²⁰ to 2¹⁹** (1 M → 512 K slots,
  24 MB → 12 MB). Re-sweep after v1.3's allocation reductions
  showed the L3-friendly sweet spot had shifted down. `mtpndd_quick_growth`
  cache-clear interval stays at 2²² puts.
- **`cacheGet` fast-miss exit on fingerprint mismatch** — skips the
  second `seq.Load` verification since a mismatch is safely a miss
  regardless of writer state. Only the hit path needs the seqlock's
  post-read check.

## 2026-04-23 — v1.3: Go leads C by 14–21 % on all N=10..13

Measured against `build/mtpndd/mtpndd_nqueens_benchmark` (the canonical
C benchmark binary):

| N  | C (s) | Go v1.3 (s) | Go / C | Advantage |
| -- | ----- | ----------- | ------ | --------- |
| 10 | 0.10  | 0.08        | 0.79×  | 21 %      |
| 11 | 0.36  | 0.30        | 0.85×  | 15 %      |
| 12 | 1.66  | 1.44        | 0.86×  | 14 %      |
| 13 | 9.35  | 7.47        | 0.80×  | 20 %      |

### Changed

- **`mk` copies the caller's edges slice on miss** instead of taking
  ownership. Makes it safe for callers to pass transient (often
  stack-backed) buffers. Adds a small copy to the miss path.
- **Every sequential op path** (`andSameField`, `andDiffField` non-parallel
  branches, `orSameField`, `orDiffField`, `Not`, `Exist`) now uses a
  stack-resident `[16]edge` buffer instead of `make([]edge, 0, N)`.
  Go escape analysis confirms zero heap allocation for these buffers.
  Net ~9 M fewer allocations per N=13 run.

### Fixed

- **Documentation** now compares against `mtpndd_nqueens_benchmark`
  (the canonical benchmark binary) rather than `mtpndd_nqueens_test`.
  The latter's nodetable config overflows at N ≥ 13 and reports
  time-to-failure instead of solve time; earlier docs cited those
  misleading numbers. See `performance.md` for the details and the
  memory note `reference_c_benchmark_binary.md`.

## 2026-04-23 — v1.2: open-addressing tables, hash-only matching

N=12: 1.82 → 1.81 s (flat). N=13: 11.2 → 8.17 s (−27 %).

### Added

- Lock-free node-slab fast path. Allocation is one `atomic.Int64.Add`
  plus one atomic chunk-pointer load; the mutex is only taken when a
  previously unused chunk index is first populated. Chunk directory is
  a fixed-capacity `[nddMaxChunks]atomic.Pointer[[]Node]` (1 G node
  upper bound).

### Changed

- **Unique tables** (NDD and BDD) now use linear-probed open-addressing
  hash tables instead of Go's builtin `map[uint64][]*Node`. At 9 M
  nodes the map was evicting L3 heavily; the new design keeps probe
  sequences in one contiguous slice. `mk` flat time at N=13 drops from
  18 % to 11 % ish depending on step.
- **Op-cache slots** shrunk from 40 B to 24 B by replacing
  `(tag, aux, idA, idB)` with a single 64-bit fingerprint. Reduces
  working-set footprint; collision probability ~2⁻⁶⁴.
- **Unique-table probe** no longer calls `edgesEqual` / `nodeKey`
  struct compare on hash match. The 64-bit hash is treated as the full
  key; avoids a cache-line load of the candidate node's edge slice.

### Risk note

Hash-equality is trusted as a full key match in two places:
1. Op cache: 64-bit fingerprint of `(tag, aux, idA, idB)`.
2. Unique tables (NDD and BDD): 64-bit XOR hash of edge set / nodeKey.

Collision probability is ~2⁻⁶⁴ per pair of distinct keys, which is well
below any observable threshold. If a workload ever trips this (billions
of distinct keys in flight), the `edgesEqual` and full `nodeKey`
compare are still present in the source as `//nolint:unused` functions
and can be reactivated with a config flag.

## 2026-04-23 — v1.1: core infrastructure in place

N=12 wall time: 7.10 s → 1.82 s (3.9 × speedup). Detailed optimization
history in [performance.md](performance.md).

### Added

- `mtpndd.Reset()` and `internal/bdd.Reset()` entry points that clear
  unique tables and op caches. Needed because v1.1 holds strong
  references to every canonical node for the lifetime of the process.
- Node slab allocators (`nddSlabState` / `bddSlabState`) — bump
  allocator over 256 K-node chunks behind a `sync.Mutex`.

### Changed

- **Op cache** now uses inline slots with a seqlock protocol. Previous
  design heap-allocated a 40 B entry per put (~200 MB of garbage per
  N=12 run).
- **Op-cache operand validation** compares per-node monotonic `uint64`
  IDs instead of pointer-equal weak references. Drops
  `runtime.weak.Make` from the hot path.
- **Op-cache result** is a strong `*Node`; cache is zeroed every
  `1 << 22` puts to bound the pin set.
- **Unique table** stores strong `*Node`s instead of `weak.Pointer[Node]`.
  Trades "nodes die when unreferenced" for "nodes die on `Reset()`",
  but drops `weak.Pointer` bookkeeping out of the hot path (it was
  previously ~40 % of total CPU via `runtime.getOrAddWeakHandle`).
- **Edge-list sort** is now an open-coded insertion sort. `sort.Slice`
  was 7 % of CPU on short lists.
- **BDD spawn gate** (`internal/bdd.subproblemBig`) always returns
  false. NDD-level parallelism suffices; BDD spawning was pure
  goroutine overhead on this workload.

### Fixed

- **N=10 sat-count bug**: initial cache design keyed on `uintptr`
  operands; after GC freed and the allocator reused an address, cache
  lookups could silently alias the old operand and return a stale
  result. Manifested as `SatCount = 1418` instead of the expected 724
  for N=10. Now operands are validated by `id uint64` which is never
  reused.

## 2026-04-22 — v1.0: initial Go port

First working pure-Go implementation. Correct on N = 1..12 but 3.6 ×
slower than C on N=12 (7.10 s vs 1.96 s).

### Scope

- Pure-Go BDD with True/False terminals and the apply operations needed
  by the NDD layer.
- Pure-Go NDD with field system, canonical `mk`,
  And/Or/Not/Diff/Exist.
- Goroutine-based spawn/sync helper in place of Lace.
- CLIs: `cmd/nqueens`, `cmd/nqueens-bench` (with `-cpuprofile`).
- Unit tests: correctness for N = 1..10.

### Out of scope (deferred)

- Multi-terminal leaves / Phase‑1 arithmetic.
- JNI / Java bridge.
- Sylvan / Lace compatibility layer.
