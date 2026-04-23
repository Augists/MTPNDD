# Changelog — mtpndd-go

Dates reflect commits on the `feature/go` branch.

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
