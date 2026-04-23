# mtpndd-go

Pure-Go implementation of **Multi-Terminal Parallel Network Decision
Diagrams**. An in-memory BDD + NDD library with a small public surface
and goroutine-based parallelism. Ships as both a Go package and a
`cgo -buildmode=c-shared` `libmtpnddjni.so` drop-in for the existing
Java `org.ants.mtpndd` wrapper.

On the `sre-ndd` network-verification workloads (single run, 4 workers,
vs. mtpndd-c / Sylvan+Lace C backend):

| workload        | Go v1.17 | C      | Go / C |
| --------------- | -------- | ------ | ------ |
| ft08 MF=3 (w=4) | 14.4 s   | 32.4 s | 0.44×  (**2.25× faster**) |
| ft12 MF=1 (w=4) | 19.8 s   | 26.5 s | 0.75×  (1.34× faster) |
| ft12 MF=3 (w=4) | 218.9 s  | 565.8 s | 0.39× (**2.60× faster**) |

On the smaller n-queens benchmark, `mtpndd-go` is **14–22 % faster**
than the C reference at N = 10..13 on 6 workers.

See [`docs/performance.md`](docs/performance.md) for the optimization
journey and [`docs/benchmarking.md`](docs/benchmarking.md) for
reproduction.

## Scope (v1.17)

- Pure-Go BDD (`internal/bdd`) with `True` / `False` terminals,
  `Mk / IthVar / NIthVar / And / Or / Xor / Not / Exist / SatCount`.
- NDD layer (package `mtpndd`) with field system, canonical `mk`, and
  `And / Or / Not / Diff / Exist`. Only two terminals in v1;
  multi-terminal leaves (fractions / doubles) and Phase‑1 arithmetic
  are intentionally deferred.
- Goroutine-based spawn/sync (`internal/work`) — no Lace, no Chase–Lev
  deque. The Go scheduler handles migration; admission is gated by a
  `GOMAXPROCS`-sized semaphore.
- CLIs at `cmd/nqueens` (correctness check) and `cmd/nqueens-bench`
  (benchmark harness with `-cpuprofile`).
- `jni/` cgo bridge producing `libmtpnddjni.so` — drop-in replacement
  for mtpndd-c's .so, used by `sre-ndd` via JNI.

## Quickstart

```bash
go build ./...
go test ./...            # N = 1..10 correctness tests
go run ./cmd/nqueens 12  # 14200 solutions
```

## Public API

```go
import "github.com/Augists/mtpndd-go"

engine := mtpndd.NewEngine()
f1 := engine.DeclareField(/*bitWidth=*/8)
f2 := engine.DeclareField(8)
engine.GenerateFields()

x := engine.Var(f1, 0)         // "bit 0 of field 1 is 1"
y := engine.NotVar(f2, 3)      // "bit 3 of field 2 is 0"

n := mtpndd.And(x, y)
n = mtpndd.Or(n, x)
n = mtpndd.Exist(n, f1.ID)

count := mtpndd.SatCount(n, engine)
```

All NDD nodes are interned; pointer equality implies semantic
equality. Nodes live until `mtpndd.Reset()` is called; see
[`docs/architecture.md`](docs/architecture.md) for the lifetime model.

## Documentation

- [docs/README.md](docs/README.md) — scope, headline numbers, doc index.
- [docs/architecture.md](docs/architecture.md) — package layout, data
  structures, concurrency primitives.
- [docs/design-decisions.md](docs/design-decisions.md) — the ten key
  design decisions with rejected alternatives.
- [docs/performance.md](docs/performance.md) — full optimization
  journey with per-step profiles (v1.0 → v1.4).
- [docs/benchmarking.md](docs/benchmarking.md) — reproduction recipe.
- [docs/changelog.md](docs/changelog.md) — per-version deltas.
- [docs/roadmap.md](docs/roadmap.md) — remaining performance levers.

## Requirements

- Go ≥ 1.26 (uses `atomic.Pointer[T]`, `weak` package for some paths,
  `runtime.AddCleanup` semantics).
- Linux / macOS / Windows. Benchmarks measured on Linux x86_64.

## License

See [LICENSE](LICENSE).
