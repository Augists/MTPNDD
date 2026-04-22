# Benchmarking mtpndd-go

Reproduction instructions for the numbers in `performance.md`.

## Environment assumed

- Go ≥ 1.26 (uses the `weak` package for some fallback paths and
  `runtime.AddCleanup` semantics; runtime also needs the 1.24-era
  `atomic.Pointer[T]` parameterized types).
- Linux x86_64, 6 cores (the measurements in this directory were all
  taken at `GOMAXPROCS=6` on a consumer-class Zen machine with a
  shared 32 MB L3 — memory-bandwidth numbers will vary with cache
  hierarchy).
- The C reference build must be present at
  `build/mtpndd/mtpndd_nqueens_benchmark`. If it's missing, build it
  with `cmake --build build -j`.

## Running the Go benchmark

```bash
go build -o /tmp/nq ./cmd/nqueens-bench
/tmp/nq -sizes 4,6,8,10,11,12,13
```

Output format (one row per N):

```
# workers=6
N   solutions  expected   time_s     nodes
...
```

- `time_s` is wall-clock for the full run (construct formula + sat-count).
- `nodes` is `mtpndd.TableSize()` — the number of live NDD nodes at
  the end of the run.
- Run the same benchmark 3-5 times back to back and take the median;
  variance is dominated by GC timing.

Flags:

- `-workers=N` — override `GOMAXPROCS` (default: value from env).
- `-cpuprofile=PATH` — write `runtime/pprof` CPU profile to `PATH`.

## Running the C reference

```bash
build/mtpndd/mtpndd_nqueens_benchmark 12
```

Output is a single tab-separated line `\t<seconds>\t<solutions>`.
The benchmark binary **must** be `mtpndd_nqueens_benchmark`, not
`mtpndd_nqueens_test`. See `performance.md` → "Two C binaries, very
different answers" for why.

## One-shot head-to-head

```bash
# Go (with sweep), median of 3:
for i in 1 2 3; do /tmp/nq -sizes 12 2>&1 | tail -1; done
for i in 1 2 3; do /tmp/nq -sizes 13 2>&1 | tail -1; done

# C:
for i in 1 2 3; do build/mtpndd/mtpndd_nqueens_benchmark 12 2>&1 | tail -1; done
for i in 1 2 3; do build/mtpndd/mtpndd_nqueens_benchmark 13 2>&1 | tail -1; done
```

## Profiling the Go code

```bash
/tmp/nq -sizes 13 -cpuprofile /tmp/nq13.prof
go tool pprof -top -cum /tmp/nq13.prof | head -30
go tool pprof -list 'mtpndd-go\.mk$' /tmp/nq13.prof
```

The flat time on `nodetable.go:slot.node == nil` is a useful quick
indicator of memory-bandwidth pressure on the unique table probe. The
flat time on `opcache.go:seq1&1 != 0` indicates the same for the
op-cache slot fetch.

## Checking escape analysis after a refactor

```bash
go build -gcflags='-m' ./... 2>&1 | grep -E "^\./ops\.go" \
  | grep -E "stack|does not escape|escapes to heap"
```

This is how v1.3's stack-buffer optimization was validated: nearly all
sequential-path edge slices report "does not escape" after the change.

## Running correctness tests

```bash
go test ./...                # ~0.5 s, N=1..10
go test -run TestNQueensLarger -timeout 180s ./...  # adds N=9,10 under load
```

All N = 1..12 sat counts must match the known n-queens numbers
(1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200). The CLI
`cmd/nqueens` also verifies this on each run and exits non-zero on
mismatch.

## When numbers disagree with `performance.md`

Most common causes, in order of frequency:

1. **Thermal throttling.** First run is cold; second is hot; third
   can be slower again as thermal limits kick in. Take the median of
   three back-to-back runs after warming the CPU.
2. **GOMAXPROCS mismatch.** The `-workers=N` flag overrides the
   environment. The docs assume 6.
3. **Wrong C binary.** `mtpndd_nqueens_test` fails at N ≥ 13 — see
   `performance.md`.
4. **`GOGC` set in the environment.** v1.3 is stable with default
   `GOGC=100`; setting `GOGC=off` gives ~10 % additional wins on
   N = 12 at the cost of ~2× peak heap.
