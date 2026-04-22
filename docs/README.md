# mtpndd-go documentation

Pure-Go port of MTPNDD (Multi-Terminal Parallel Network Decision Diagrams).
The C original (still present at the repo root in `mtpndd/`, `sylvan/`, `jni/`)
remains the reference implementation. This directory documents only the Go
rewrite on the `feature/go` branch.

Pre-existing C/Sylvan/Lace documentation was moved to `docs_c_legacy/` during
the Go port; look there for history of the C implementation.

## Scope of the Go port (v1)

- Pure-Go BDD library with True / False terminals, `ithvar`, `and`, `or`,
  `not`, `xor`, `exist`, `satcount`.
- Pure-Go NDD layer with field declaration, canonical `mk`, and the five
  boolean operations (`And`, `Or`, `Not`, `Diff`, `Exist`).
- Goroutine-based parallelism — Lace is not ported.
- Multi-terminal leaves (`fraction`, `double`) and Phase‑1 arithmetic
  (`plus`, `minus`, `times`, `divide`, `abstract_plus`) are intentionally
  omitted in v1.
- JNI / Java surface is not ported in v1; the v1 consumer is the `cmd/nqueens`
  and `cmd/nqueens-bench` tools plus the `mtpndd` Go package.

## Documents

- [architecture.md](architecture.md) — package layout, data structures,
  lifetime model (GC vs. weak vs. strong refs), concurrency primitives.
- [design-decisions.md](design-decisions.md) — the 10 key design
  choices and rejected alternatives, cross-referenced to
  `performance.md` where number-driven.
- [performance.md](performance.md) — n-queens benchmark numbers at
  each optimization step, CPU profiles, comparison against the C
  version, the "two C binaries" clarification.
- [benchmarking.md](benchmarking.md) — exact reproduction recipe for
  every number in `performance.md`, including profiling workflow and
  escape-analysis verification.
- [changelog.md](changelog.md) — per-version (v1.0 → v1.3) summary
  of what changed and what the wall-clock impact was.
- [roadmap.md](roadmap.md) — remaining performance gaps and the
  planned next round of optimizations.

## Quick facts

- Go version: 1.26. Uses `runtime/pprof` for profiling.
- Module path: `github.com/Augists/mtpndd-go`.
- Primary benchmark: `cmd/nqueens-bench -sizes 12` at `GOMAXPROCS=6`.
- C reference binary: **`build/mtpndd/mtpndd_nqueens_benchmark`** (not
  `mtpndd_nqueens_test` — the latter uses a smaller nodetable config that
  overflows at N ≥ 13 and reports the time-to-failure rather than a solve).
- Current wall-clock (6 workers, median of multiple runs):

  | N  | C      | Go     | Go / C | Go advantage |
  | -- | ------ | ------ | ------ | ------------ |
  | 10 | 0.09 s | 0.07 s | 0.80×  | 20 % faster  |
  | 11 | 0.36 s | 0.30 s | 0.86×  | 14 % faster  |
  | 12 | 1.66 s | 1.38 s | 0.83×  | 17 % faster  |
  | 13 | 9.43 s | 7.40 s | 0.78×  | 22 % faster  |

  Go leads C by 14–22 % across all tested N on n-queens.
