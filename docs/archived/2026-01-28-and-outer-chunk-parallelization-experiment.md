# 2026-01-28 AND Outer-Chunk Parallelization Experiment

## Motivation

The current same-field AND uses fine-grained item tasks (one task per edge-pair). This can create too many small tasks and overhead. We want an alternate scheduling mode that coarsens tasks by *outer-edge chunks* and only parallelizes these larger chunks, while preserving the original `(child_a, child_b)` operand order for op-cache stability.

## Root Cause / Hypothesis

- Fine-grained tasks can saturate the Lace deque with small work units, increasing overhead and contention.
- Coarser-grained chunking might reduce overhead and improve scaling when each chunk contains enough work.

## Implementation

**Files**
- `sylvan/src/sylvan/mtpndd/mtpndd_node.c`
- `sylvan/src/sylvan/mtpndd/CMakeLists.txt`
- `sylvan/src/sylvan/mtpndd/test/and_microbench.c`

**Key changes**
- Added an experimental toggle via `MTPNDD_AND_OUTER_CHUNK` (env var).
- In same-field AND, choose the **outer list** by `edge_count` (`outer = max(a,b)`), but **do not swap** the recursion operand order.
- Each chunk task builds a **local edge map** (dedups `(child,label)` within chunk) and returns it to the parent for serial merge.
- Parent merges results into the final edge map and transfers child refs into `temp_refs`.

**Parameters (tuned)**
- `MTPNDD_AND_OUTER_CHUNK_MIN_OUTER=64`
- `MTPNDD_AND_OUTER_CHUNK_TASKS_PER_WORKER=4`
- `MTPNDD_AND_OUTER_CHUNK_MIN_CHUNK=8`
- Chunking is only used when: `outer_edges >= MIN_OUTER` and product is large enough.
- Task count target: `workers * TASKS_PER_WORKER`, with a minimum chunk size enforced.

## Verification

**Build**
```
cd .worktrees/and-outer-chunk/sylvan
cmake -S . -B build-rel -DMTPNDD_ENABLE_RECORDING=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-rel -j
ctest --test-dir build-rel
```

**N-Queens (n=12, 4 cores pinned)**
```
taskset -c 0-3 env MTPNDD_AND_OUTER_CHUNK=0 ./sylvan/build-rel/src/sylvan/mtpndd/mtpndd_nqueens_test 12
taskset -c 0-3 env MTPNDD_AND_OUTER_CHUNK=1 ./sylvan/build-rel/src/sylvan/mtpndd/mtpndd_nqueens_test 12
```

Results (solutions=14200 both):
| Mode | Time (s) |
|------|----------|
| outer-chunk OFF | 9.478 |
| outer-chunk ON (tuned) | 8.761 |

**Microbench (same-field AND)**
```
env MTPNDD_AND_OUTER_CHUNK=0 ./sylvan/build-rel/src/sylvan/mtpndd/mtpndd_and_microbench shallow 64 32 50 4
env MTPNDD_AND_OUTER_CHUNK=1 ./sylvan/build-rel/src/sylvan/mtpndd/mtpndd_and_microbench shallow 64 32 50 4
env MTPNDD_AND_OUTER_CHUNK=1 ./sylvan/build-rel/src/sylvan/mtpndd/mtpndd_and_microbench shallow 32 64 50 4

env MTPNDD_AND_OUTER_CHUNK=0 ./sylvan/build-rel/src/sylvan/mtpndd/mtpndd_and_microbench mixed 64 32 20 4 3 4
env MTPNDD_AND_OUTER_CHUNK=1 ./sylvan/build-rel/src/sylvan/mtpndd/mtpndd_and_microbench mixed 64 32 20 4 3 4
```

Observed:
| Mode | Case | Iter seconds (approx) |
|------|------|-----------------------|
| shallow | OFF | 0.000151 |
| shallow | ON (tuned) | 0.000145 |
| shallow | ON (A<B, tuned) | 0.000139 |
| mixed   | OFF | 0.002421 |
| mixed   | ON (tuned) | 0.002410 |

Larger cases (4 workers, pinned):
| Mode | Case | Iter seconds (approx) |
|------|------|-----------------------|
| shallow | 128x64 OFF | 0.0005826 |
| shallow | 128x64 ON  | 0.0003320 |
| shallow | 256x128 OFF | 0.0018269 |
| shallow | 256x128 ON  | 0.0015335 |
| mixed   | 128x64 OFF | 0.0074732 |
| mixed   | 128x64 ON  | 0.0069669 |
| mixed   | 256x128 OFF | 0.0577898 |
| mixed   | 256x128 ON  | 0.0580698 |

## Notes / Risks

- Coarse chunking reduces task overhead, but can also reduce available parallel slack in shallow workloads.
- An earlier (more aggressive) configuration improved N-Queens more but slowed shallow microbench; the tuned thresholds keep shallow on-par while retaining some improvement on N-Queens.
- The experiment preserves op-cache operand ordering to avoid changing caching semantics.

## Follow-ups

- Add more mixed-depth benchmarks and a workload with broader edge-count variance.
- Consider dynamic chunk sizing based on observed per-task runtime (feedback-driven scheduling).
