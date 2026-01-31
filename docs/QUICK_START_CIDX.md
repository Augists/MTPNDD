# Quick Start: feature/cidx Development

Fast reference for parallelization feature development.

## The 10-Minute Overview

**What**: Implement parallel MTPNDD operations (AND, OR, DIFF, etc.)

**Why**: feature/index optimized memory (arrays vs. hashmap), now optimize time (parallelization)

**How**: Use Lace task framework to spawn independent recursive branches

**Success**: Performance improves 2-3x on 4 workers, memory stays stable

---

## Standard Development Cycle

### 1. Establish Baseline (15 minutes)

```bash
# Get latest feature/index code
git checkout feature/index
cd sylvan
cmake -B build && cmake --build build

# Measure performance
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1

# Save output, note Peak RSS and time
# Return to development
git checkout feature/cidx
```

### 2. Design Parallelization (30 minutes)

Create `docs/features/parallel-[operation]-design.md` with:
- Current serial code flow (pseudocode or ASCII diagram)
- Which parts can parallelize?
- Which data structures need synchronization?
- Expected speedup and risks

### 3. Implement (1-2 hours)

Create `feat/parallel-[operation]` branch:

```bash
git checkout -b feat/parallel-[operation]
cd sylvan
```

**Minimal parallel template**:

```c
// In mtpndd_node.c
mtpndd_t mtpndd_and_parallel(mtpndd_t a, mtpndd_t b) {
    // Check cache, terminals, get cofactors (same as before)

    // NEW: Spawn one branch as task
    LACE_NEWFRAME();
    mtpndd_t left_result;
    lace_spawn_task(task_and_low, &left_result, a_low, b_low);

    // Execute other branch in current worker
    mtpndd_t right_result = mtpndd_and_parallel(a_high, b_high);

    // Wait for spawned task
    lace_yield(LACE_SYNC_ALL);

    // Combine results (same as before)
    return mtpndd_makenode(v, left_result, right_result);
}
```

**Test frequently**:
```bash
cmake -B build -DMTPNDD_ENABLE_RECORDING=ON
cmake --build build
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1  # Single worker first!
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 4  # Multi-worker
```

### 4. Measure Performance (30 minutes)

```bash
# Clean build without instrumentation
cmake -B build
cmake --build build

# Run baseline and optimized
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1 > /tmp/baseline.txt
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 4 > /tmp/optimized.txt

# Manually compare or create table in feature document
```

### 5. Document Results (30 minutes)

Update `docs/features/parallel-[operation]-design.md`:

**Add Testing Results section**:
```markdown
## Testing Results

### Metrics (N=12)

| Aspect | Baseline | Parallel-4w | Change |
|--------|----------|-------------|--------|
| Peak RSS | 772 MB | 780 MB | +1% ✅ |
| Time (-w1) | 45.2s | 46.1s | -2% (overhead) |
| Time (-w4) | 45.2s | 13.5s | 3.35x faster ✅ |

### Lessons
- Work stealing balanced load well
- Task overhead minimal except single-worker
- Consider threshold for tiny tasks
```

### 6. Commit and Merge (10 minutes)

```bash
git add -A
git commit -m "feat(cidx): parallelize mtpndd_and

- Spawn low branch as Lace task
- Execute high branch in current worker
- Results: 3.35x speedup on 4 workers, 84% efficiency
- See: docs/features/parallel-mtpndd-and.md"

git checkout feature/cidx
git merge --no-ff feat/parallel-[operation]
git branch -d feat/parallel-[operation]
```

**Total time: ~4-5 hours per feature**

---

## Key Files You'll Edit

1. **mtpndd_node.c** - Implement parallel operation (AND, OR, etc.)
2. **mtpndd_node.h** - Declare parallel function if needed
3. **docs/features/parallel-[op].md** - Document everything
4. **CLAUDE.md** - Update if you change overall structure

---

## Common Patterns

### Pattern 1: Parallel Two-Branch Operation (AND, OR)

```c
mtpndd_t mtpndd_and_parallel(mtpndd_t a, mtpndd_t b) {
    // ... check cache, terminals, get cofactors ...

    LACE_NEWFRAME();
    mtpndd_t left;
    lace_spawn_task(task_and_low, &left, cofactor_low_a, cofactor_low_b);

    mtpndd_t right = mtpndd_and_parallel(cofactor_high_a, cofactor_high_b);

    lace_yield(LACE_SYNC_ALL);

    mtpndd_t result = mtpndd_makenode(v, left, right);
    return result;
}
```

**Time: 2-3x speedup expected**

### Pattern 2: Parallel Reduction (satcount)

```c
// For reduction operations, use parallel reduction with atomic accumulation
uint64_t count_low = mtpndd_satcount_parallel(low_node);
uint64_t count_high = mtpndd_satcount_parallel(high_node);
return (1ULL << var) * (count_low + count_high);
```

**Time: Near-linear speedup expected (N workers → N speedup)**

### Pattern 3: Handling Non-Parallelizable Parts

Some operations can't parallelize well (e.g., mknode). That's OK - parallelize only the expensive parts.

---

## Troubleshooting Quick Fixes

| Problem | Quick Fix |
|---------|-----------|
| Results differ (-w1 vs -w4) | Race condition. Check operation cache and node table mutations. |
| Performance worse with -w4 | Task overhead. Add granularity threshold (skip parallelization for small tasks). |
| Memory increases 10%+ | Check LACE_NEWFRAME/LACE_DELETEFRAME balanced. Verify temp_refs cleanup. |
| Crashes with -w2 but not -w1 | Memory corruption. Use Valgrind memcheck: `valgrind --leak-check=full ./binary` |
| Hangs (deadlock) | Check `lace_yield()` called after every spawn. No nested spawns without yield. |

**For any issue**: Check `Troubleshooting` section in WORKFLOW_GUIDE.md

---

## Success Criteria Checklist

Before considering feature "done":

✅ **Correctness**
- Compiles without warnings
- Results match baseline (-w1, -w4)
- No Valgrind errors: `valgrind ./binary`

✅ **Performance**
- Speedup > 5% on 4+ workers
- Single-worker regression acceptable (< 5%)
- Memory stable (< 5% increase)

✅ **Documentation**
- Feature document complete
- Code comments explain parallelization
- CLAUDE.md updated (if applicable)

✅ **Git**
- Clean commits with clear messages
- Feature linked to documentation
- Ready to merge to feature/cidx

---

## Example: Parallelizing mtpndd_and

Complete walkthrough (not meant to be literal, adapt to actual codebase):

### Step 1: Design
```
Current mtpndd_and:
  1. Check cache
  2. Handle terminals (return immediately if found)
  3. Get variable order
  4. Compute left = AND(a_low, b_low)    <- SLOW
  5. Compute right = AND(a_high, b_high) <- SLOW
  6. Make node with (left, right)

Parallelization:
  - Spawn step 4, execute step 5 in current worker
  - Sync before step 6
```

### Step 2: Implement
```c
mtpndd_t mtpndd_and_parallel(mtpndd_t a, mtpndd_t b) {
    // Cache check (unchanged)
    mtpndd_t cached = operation_cache_lookup(AND_OP, a, b);
    if (cached != MTPNDD_NULL) return cached;

    // Terminals (unchanged)
    if (a == MTPNDD_TRUE) return b;
    if (b == MTPNDD_TRUE) return a;
    // ... etc

    // NEW: Parallelize
    LACE_NEWFRAME();
    mtpndd_t left;
    lace_spawn_task(mtpndd_and_parallel_task, &left, a_low, b_low);

    mtpndd_t right = mtpndd_and_parallel(a_high, b_high);

    lace_yield(LACE_SYNC_ALL);

    mtpndd_t result = mtpndd_makenode(var, left, right);
    operation_cache_insert(AND_OP, a, b, result);
    return result;
}

// Helper task function
void mtpndd_and_parallel_task(mtpndd_t *result, mtpndd_t a, mtpndd_t b) {
    *result = mtpndd_and_parallel(a, b);
}
```

### Step 3: Test
```bash
./build/test/mtpndd_nqueens_exp 12 1   # Must match baseline
./build/test/mtpndd_nqueens_exp 12 4   # Should be faster
```

### Step 4: Measure
```bash
# Compare with baseline
# Expect: ~3x speedup on 4 workers
```

### Step 5: Document
```markdown
# Feature: Parallel mtpndd_and

## Problem
AND operation is called millions of times in recursive algorithms.
Sequential evaluation of low/high branches wastes parallelism.

## Solution
Spawn low branch as Lace task, execute high in current worker.
Synchronize before combining results.

## Results
- 4 workers: 3.35x speedup (84% efficiency)
- Memory: +1% (acceptable)
- Single-worker: -2% (task frame overhead, acceptable)

## Implementation
- Modified: mtpndd_node.c (mtpndd_and function)
- Added: mtpndd_and_parallel_task helper
- Synchronization: Lace work-stealing scheduler

See: mtpndd_node.c:1150-1200
```

### Step 6: Commit
```bash
git commit -m "feat(cidx): parallelize mtpndd_and

- Spawn low branch as task, execute high in worker
- 3.35x speedup on 4 workers, 84% efficiency
- See: docs/features/parallel-mtpndd-and.md"
```

Done! Now parallelize mtpndd_or using the same pattern.

---

## Next Steps After First Feature

1. **Parallel OR** (similar pattern to AND)
2. **Parallel satcount** (reduction pattern)
3. **Performance tuning** (task granularity threshold)
4. **Parallel DIFF, EXIST** (build on AND/OR framework)

Each should follow the same 4-5 hour cycle.

---

## More Information

- Full workflow: See **docs/WORKFLOW_GUIDE.md**
- Project overview: See **CLAUDE.md**
- Detailed analysis: See **docs/memory_optimization_analysis.md**
- Lace framework: See **sylvan/src/lace/** (headers and examples)
