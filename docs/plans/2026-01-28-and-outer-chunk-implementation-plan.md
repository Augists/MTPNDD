# AND Outer-Chunk Parallelization Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement task-by-task (we are executing in this worktree directly).

**Goal:** Add an experimental, coarser-grained scheduling mode for `mtpndd_and` (same-field case) that parallelizes by *outer-edge chunks* and evaluates it with microbenchmarks (not only NQueens).

**Architecture:** Keep `mtpndd_and_rec(child_a, child_b)` operand order stable to preserve op-cache behavior. Only choose the *outer iteration list* by `edge_count` (prefer outer=A when `|A| >= |B|`). Chunk tasks build a local edge map (dedup within chunk), then the parent merges results serially into the final `res_edges`.

**Tech Stack:** C11, Lace tasks (`SPAWN/SYNC/DROP`), Sylvan BDD ref-counting, MTPNDD edge maps + nodetable.

---

### Task 1: Add Chunk Task + Merge Helpers

**Files:**
- Modify: `sylvan/src/sylvan/mtpndd/mtpndd_node.c`

**Steps:**
1. Add a `TASK_DECL_n`/`TASK_IMPL_n` for `mtpndd_and_same_field_chunk(...)` returning a struct containing `{status, mtpndd_edge_t* local_edges}`.
2. Implement a local, chunk-private edge insertion helper that refs a child *only once per unique child* (avoid ref inflation on label merges).
3. Implement parent-side merge helper that *moves* labels out of `local_edges` (via `atomic_exchange`) and transfers the owned child refs into `temp_refs`.
4. Add a robust cancel path: for stolen tasks `SYNC` then cleanup; for not-stolen tasks `DROP`.

**Expected verification (manual):**
- `ctest --test-dir sylvan/build-rel` still passes.
- `mtpndd_nqueens_test 12` still outputs solutions=14200.

---

### Task 2: Wire Outer-Chunk Mode Behind a Toggle

**Files:**
- Modify: `sylvan/src/sylvan/mtpndd/mtpndd_node.c`

**Steps:**
1. Add a one-time env toggle helper (e.g. `MTPNDD_AND_OUTER_CHUNK=1`) and a small activation threshold (only chunk when outer edge count is "large enough").
2. In the same-field case, select outer/inner edge lists by `edge_count`, but keep recursive calls in original `(child_from_a, child_from_b)` order.

**Expected verification (manual):**
- With toggle OFF: behavior matches baseline.
- With toggle ON: behavior matches baseline (same solutions/satcount).

---

### Task 3: Add Microbench Harness (Shallow + Mixed)

**Files:**
- Create: `sylvan/src/sylvan/mtpndd/test/and_microbench.c`
- Modify: `sylvan/src/sylvan/mtpndd/CMakeLists.txt`

**Steps:**
1. Implement `and_microbench` with modes:
   - `shallow`: parents have `na/nb` edges, children are small nodes (depth 1).
   - `mixed`: deeper children (depth 2-3) with configurable fanout.
2. Print wall time, lace counters (if enabled), and key MTPNDD stats when `ENABLE_RECORDING` is on.

**Expected verification (manual):**
- Runs without crash in both toggle modes.
- Demonstrates whether outer-chunk changes scaling behavior vs baseline.

---

### Task 4: Record Experiment Results

**Files:**
- Create: `docs/archived/2026-01-28-and-outer-chunk-parallelization-experiment.md`
- Modify: `docs/archived/README.md`

**Steps:**
1. Record methodology (why NQueens insufficient), exact commands, machine/core pinning, worker counts, and results tables.
2. Record whether the approach helps/hurts, and why (spawn overhead vs merge/memory pressure, cache behavior, task imbalance).

