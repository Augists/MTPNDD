# MTPNDD feature/cidx Development Workflow Guide

This guide provides a detailed step-by-step process for implementing and testing parallelization features in the feature/cidx branch.

## Overview

**Goal**: Parallelize MTPNDD logical operations on top of feature/index's array-optimized structures to maximize library efficiency.

**Baseline**: feature/index branch (array-optimized, serial execution)

**Benchmark Scenario**: nqueens N=12 (standard for all measurements)

**Development Cycle**: Problem → Analysis → Implementation → Testing → Documentation → Commit

---

## Phase 1: Establish Baseline Measurements

Before parallelizing any operation, establish exact metrics from feature/index as your reference point.

### Step 1.1: Build feature/index Baseline

```bash
# Ensure you have feature/index checked out
git checkout feature/index
cd sylvan
cmake -B build
cmake --build build
```

### Step 1.2: Capture Baseline Metrics

```bash
# Fast /proc-based measurement
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1 > /tmp/baseline.txt

# Or with detailed instrumentation
cmake -B build -DMTPNDD_ENABLE_RECORDING=ON
cmake --build build
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1 > /tmp/baseline_detailed.txt
```

### Step 1.3: Document Baseline

Create `docs/features/baseline-feature-index.md`:

```markdown
# Baseline: feature/index Measurements

**Date**: [YYYY-MM-DD]
**Branch**: feature/index
**Commit**: [git show -s --format=%H]
**Benchmark**: nqueens N=12

## Metrics

### Memory Usage
- Peak RSS: [X] MB
- Peak VMS: [Y] MB

### Time Breakdown
- Total time: [X] seconds
- AND operations: [X] seconds
- OR operations: [X] seconds
- Other operations: [X] seconds

### Memory Composition
- Node table: [X] MB
- Edge pool: [X] MB
- Hash table: [X] MB
- Other: [X] MB

### Node/Edge Statistics
- Final node count: [N]
- Final edge pool size: [N] entries
- Cache statistics: (if available)

## Reference Data

(Include full output of measure_memory.sh and nqueens_exp for future comparison)
```

### Step 1.4: Return to Development Branch

```bash
git checkout feature/cidx
```

---

## Phase 2: Design Parallelization Feature

### Step 2.1: Identify Target Operation

Choose one operation to parallelize. Recommended order:
1. **mtpndd_and** - High frequency, clear parallelization
2. **mtpndd_or** - Similar to AND
3. **mtpndd_satcount** - Reduction pattern
4. **mtpndd_exist / mtpndd_forall** - Quantifiers
5. **mtpndd_diff** - Three-way split

### Step 2.2: Analyze Current Implementation

Read the current serial implementation thoroughly:

```bash
# Example: analyzing mtpndd_and
grep -A 50 "^mtpndd_t mtpndd_and" sylvan/src/sylvan/mtpndd/mtpndd_node.c
```

Document:
- Entry point and cache checking
- Terminal cases (what returns without recursion)
- Recursive subtasks (low/high branches, dependencies)
- Final node creation
- Time-critical paths (measure with ENABLE_RECORDING if possible)

### Step 2.3: Design Parallel Strategy

Create `docs/features/parallel-[operation-name]-design.md`:

```markdown
# Design: Parallelize mtpndd_[operation]

## Problem Statement

**Bottleneck**: [Which paths are slow? Why?]
**Current approach**: Serial execution of subtasks
**Opportunity**: Independent branches can execute in parallel

## Analysis

### Current Flow
[Pseudocode or ASCII diagram of current implementation]

### Parallelization Strategy

1. **Independent subtasks**:
   - Task A (low branch): Can execute independently? YES/NO
   - Task B (high branch): Can execute independently? YES/NO
   - Task C (other): ?

2. **Synchronization needs**:
   - Operation cache: Lock-free / Locked / None?
   - Node table: Per-bucket lock / Global lock / Atomic?
   - Edge pool: Atomic append / Per-worker buffer?

3. **Task granularity**:
   - Always spawn if depth < [X]?
   - Spawn only if estimated work > [Y] nodes?
   - Threshold rationale: [explain]

### Expected Impact

- **Time**: Estimate 2-3x speedup on 4 workers
- **Memory**: No expected change (same data structures)
- **Complexity**: Medium (adds synchronization, task overhead)

### Risk Assessment

- **Race conditions**: Where could they occur? (operation cache, node table)
- **Deadlocks**: Possible with locks?
- **Performance degradation**: If tasks too fine-grained?

## Implementation Plan

### Code locations to modify
- `mtpndd_node.c`: Main operation implementation
- `mtpndd_operation_cache.c`: Cache synchronization
- `mtpndd_nodetable.c`: Node table synchronization (if needed)

### New synchronization primitives needed
- [Atomics? Locks? Work-stealing queue?]

### Integration points
- How does this interact with GC?
- How does this interact with other parallelized operations?

## Testing Strategy

1. Single worker test (correctness)
2. Multi-worker test (parallelism)
3. Stress test (high worker count + memory profiling)
4. Race detection (Valgrind helgrind if feasible)
```

### Step 2.4: Get Feedback

Review your design document:
- Does the parallelization strategy make sense?
- Are synchronization points clear?
- Are there obvious race conditions?

---

## Phase 3: Implementation

### Step 3.1: Create Feature Branch

```bash
git checkout -b feat/parallel-[operation-name]
```

### Step 3.2: Implement Changes

**Key principles**:
- Start with minimal changes (parallelize one path first)
- Use Lace task framework consistently
- Add synchronization pragmatically (not over-engineered)
- Keep commits logically organized

**Example structure for mtpndd_and**:

```c
mtpndd_t
mtpndd_and_parallel(mtpndd_t a, mtpndd_t b)
{
    // 1. Check cache (may need atomic update)
    mtpndd_t cached = operation_cache_lookup(AND, a, b);
    if (cached != MTPNDD_NULL) return cached;

    // 2. Handle terminal cases
    if (is_terminal(a)) return (a == MTPNDD_TRUE) ? b : MTPNDD_FALSE;
    if (is_terminal(b)) return (b == MTPNDD_TRUE) ? a : MTPNDD_FALSE;

    // 3. Get variable order
    uint32_t va = get_var(a), vb = get_var(b);
    uint32_t v = (va < vb) ? va : vb;

    // 4. Get cofactors
    get_cofactors(a, va, v, &a_low, &a_high);
    get_cofactors(b, vb, v, &b_low, &b_high);

    // 5. Parallelize: spawn one task, execute other in current worker
    LACE_NEWFRAME();
    mtpndd_t left_result;
    lace_spawn_task(task_and_left, &left_result, a_low, b_low);

    mtpndd_t right_result = mtpndd_and_parallel(a_high, b_high);

    lace_yield(LACE_SYNC_ALL);  // Wait for spawned task

    // 6. Create result node
    mtpndd_t result = mtpndd_makenode(v, left_result, right_result);

    // 7. Cache and return
    operation_cache_insert(AND, a, b, result);
    return result;
}
```

### Step 3.3: Build and Test Incrementally

```bash
# Build with instrumentation for debugging
cd sylvan
cmake -B build -DMTPNDD_ENABLE_RECORDING=ON
cmake --build build

# Test with single worker (correctness)
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1

# If correct, test with multiple workers
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 2
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 4
```

### Step 3.4: Commit Logically

Make small, focused commits:

```bash
git add [specific files for one change]
git commit -m "feat(cidx): add parallel [operation] - [specific aspect]

- Implemented task spawning for low/high branches
- Added synchronization for operation cache

Ongoing work on feature/parallel-[operation-name]"
```

---

## Phase 4: Performance Testing

### Step 4.1: Prepare Clean Build

```bash
cd sylvan
cmake -B build  # Production build without instrumentation
cmake --build build
```

### Step 4.2: Capture Performance Metrics

```bash
# Memory measurement
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1 > /tmp/optimized.txt

# Or with instrumentation
cmake -B build -DMTPNDD_ENABLE_RECORDING=ON
cmake --build build
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1 > /tmp/optimized_detailed.txt
```

### Step 4.3: Compare Metrics

Create comparison section in your feature document:

```markdown
## Testing Results

### Correctness
- ✅ nqueens_test 8: PASS
- ✅ nqueens_exp 12 (-w 1): Results match baseline
- ✅ nqueens_exp 12 (-w 4): Results match baseline

### Performance Metrics

#### Memory Usage (N=12)

| Metric | Baseline (feature/index) | Optimized (parallel) | Change |
|--------|--------------------------|----------------------|--------|
| Peak RSS | 772 MB | 780 MB | +1.0% ✅ |
| Peak VMS | 6158 MB | 6200 MB | +0.7% ✅ |

#### Execution Time (N=12)

| Workers | Baseline | Optimized | Speedup | Efficiency |
|---------|----------|-----------|---------|------------|
| 1 | 45.2s | 46.1s | 0.98x | N/A |
| 2 | 45.2s | 24.8s | 1.82x | 91% |
| 4 | 45.2s | 13.5s | 3.35x | 84% |

#### Operation Breakdown (with instrumentation)

| Operation | Baseline | Parallel | Speedup |
|-----------|----------|----------|---------|
| AND | 30.2s | 9.8s | 3.08x |
| OR | 12.1s | 11.5s | 1.05x |
| satcount | 2.5s | 2.3s | 1.09x |

### Analysis

- ✅ Performance improved: 1.82x on 2 workers, 3.35x on 4 workers
- ✅ Memory stable: < 2% increase
- ⚠️ Single-worker regression: 2.0% (task overhead)
- 🎯 Parallelization efficient: 84% on 4 workers (good scheduling)

### Unexpected Findings

[Document anything surprising, e.g., "Cache hit rate dropped on multi-worker"]
```

### Step 4.4: Validate with Multiple Runs

```bash
# Run 3 times to check consistency
for i in {1..3}; do
    echo "Run $i"
    ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 4 | tail -5
done
```

If results vary significantly, investigate (randomness in work scheduling, CPU contention, etc.).

---

## Phase 5: Documentation and Analysis

### Step 5.1: Complete Feature Document

Finalize `docs/features/parallel-[operation-name].md` with:

**Sections**:
1. Problem Statement
2. Analysis (from design phase)
3. Implementation (code changes, key files)
4. Testing Results (measurements, comparisons)
5. Lessons & Analysis
6. Recommendations for Next Steps

**Example Lessons section**:
```markdown
## Lessons & Analysis

### What Worked Well
- Spawning one branch and executing other in current worker balanced load well
- Operation cache synchronization with atomic CAS had minimal overhead
- Lace task scheduling handled load well with 4-8 workers

### Unexpected Challenges
- Single-worker case regressed by 2% due to task frame overhead
  - Solution: Add threshold to skip parallelization if task too small
- Cache coherency issue on NUMA systems (if applicable)
  - Solution: Consider per-worker caches for very high worker counts

### Trade-offs Made
- Chose lock-free cache updates over locking (slightly more complex, faster)
- Decided against parallelizing satcount (reduction overhead > benefit)

### Recommendations for Next Steps
1. Parallelize mtpndd_or using same pattern (expected similar speedup)
2. Investigate task granularity threshold (when is overhead acceptable?)
3. Add cache miss statistics to MTPNDD_ENABLE_RECORDING for future analysis
```

### Step 5.2: Update Main Documentation

If this is a significant feature, update relevant sections of:
- `CLAUDE.md` - If new patterns or concepts introduced
- `README.md` - If user-visible features changed

### Step 5.3: Code Comments

Add comments explaining parallelization strategy in key functions:

```c
/*
 * mtpndd_and_parallel - Parallel AND operation
 *
 * This function spawns the low branch as a Lace task and executes
 * the high branch in the current worker, then synchronizes results.
 *
 * Synchronization: Operation cache uses atomic CAS for insertion.
 * Note: Single-worker performance may be slightly lower due to
 * Lace task frame overhead. For optimal single-worker performance,
 * consider using mtpndd_and_serial() directly.
 *
 * Parallelization is beneficial with 2+ workers.
 */
```

---

## Phase 6: Commit and Integration

### Step 6.1: Final Commit

```bash
git add -A
git commit -m "feat(cidx): parallelize mtpndd_and operation

- Implemented work-stealing task parallelization
- AND operation spans low branch as task, executes high in current worker
- Added atomic operation cache synchronization
- Results: 3.35x speedup on 4 workers, 84% efficiency
- Memory stable: < 2% increase from baseline

See: docs/features/parallel-mtpndd-and.md

Co-Authored-By: [Your Name] <[email]>"
```

### Step 6.2: Merge to Main Development Branch

When complete and tested:

```bash
git checkout feature/cidx
git merge --no-ff feat/parallel-[operation-name]
git branch -d feat/parallel-[operation-name]
```

### Step 6.3: Update Baseline for Next Feature

Once parallel-[operation] is validated and merged:

```bash
# Update baseline for next feature
cp docs/features/parallel-[operation-name].md docs/features/baseline-[operation-name].md
```

Modify baseline document to note: "Current baseline after implementing parallel [operation]"

---

## Phase 7: Iterative Development

### Next Feature (e.g., Parallel OR)

1. Create new feature branch: `feat/parallel-mtpndd-or`
2. Design parallelization (likely similar to AND)
3. Implement changes
4. Test against previous baseline
5. Document and commit

### Monitoring Progress

Track metrics across all parallelized operations in `docs/OPTIMIZATION_LOG.md`:

```markdown
# Optimization Progress Log

## Baseline (feature/index)
- Peak RSS: 772 MB
- Time (N=12, -w1): 45.2s
- Time (N=12, -w4): 45.2s (no parallelization)

## After Parallel AND
- Peak RSS: 780 MB (+1%)
- Time (N=12, -w1): 46.1s (-2% single-worker)
- Time (N=12, -w4): 13.5s (3.35x speedup)

## After Parallel OR
- (To be measured)

## After Parallel satcount
- (To be measured)
```

---

## Troubleshooting

### Issue: Results differ between single and multi-worker runs

**Diagnosis**:
- Race condition in synchronization primitive
- Uninitialized variables in parallel paths
- Cache coherency issue

**Solution**:
- Run with Valgrind: `valgrind --tool=helgrind ./build/.../mtpndd_nqueens_exp 12 2`
- Add extensive logging to trace execution
- Revert to serial version and test incrementally

### Issue: Performance degrades with more workers

**Diagnosis**:
- Task overhead exceeds parallelization benefit
- Contention on shared data structures (operation cache, node table)
- Poor load balance (uneven task distribution)

**Solution**:
- Add task granularity threshold (don't parallelize small tasks)
- Profile with `-w 1, 2, 4` to identify threshold
- Consider lock-free data structures if contention suspected

### Issue: Memory usage increases unexpectedly

**Diagnosis**:
- Lace task frames accumulating (not cleaned up)
- Temp_refs not being released
- Additional buffers allocated per worker

**Solution**:
- Check `lace_deleteframe()` is called after `lace_newframe()`
- Verify temp_refs cleanup in GC
- Profile memory allocation with Valgrind massif

### Issue: Build fails with parallelization changes

**Diagnosis**:
- Missing includes (lace.h, synchronization headers)
- Incompatible with Lace API version
- CMake configuration needs update

**Solution**:
- Check Lace headers: `sylvan/src/lace/lace.h`
- Verify Lace initialization in mtpndd_common.c
- Update CMakeLists.txt if new dependencies added

---

## Checklist for Feature Completion

Before merging a parallelization feature to feature/cidx:

- [ ] **Correctness**
  - [ ] Code compiles without warnings
  - [ ] Tests pass with `-w 1` (single worker)
  - [ ] Tests pass with `-w 4` (multi-worker)
  - [ ] Valgrind memcheck: no leaks or errors
  - [ ] Results match baseline (bit-for-bit or expected differences documented)

- [ ] **Performance**
  - [ ] Speedup > 5% on 4+ workers
  - [ ] Single-worker regression < 5% (acceptable due to task overhead)
  - [ ] Memory stable (< 5% increase)
  - [ ] Efficiency reasonable (> 70% on 4 workers)

- [ ] **Documentation**
  - [ ] Feature document complete (problem, analysis, results, lessons)
  - [ ] Code comments explain parallelization strategy
  - [ ] CLAUDE.md updated (if new patterns introduced)
  - [ ] Commit messages clear and traceable

- [ ] **Integration**
  - [ ] Changes logically isolated (can review easily)
  - [ ] No conflicts with other pending features
  - [ ] Baseline updated for next feature

---

## Quick Reference

### Build Commands
```bash
# Development (with instrumentation)
cmake -B build -DMTPNDD_ENABLE_RECORDING=ON
cmake --build build

# Production (clean build)
cmake -B build && cmake --build build --clean-first

# Clean everything
rm -rf build && cmake -B build && cmake --build build
```

### Test Commands
```bash
# Correctness
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_test 8

# Performance (N=12, 1/2/4 workers)
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 2
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 4

# Memory profiling
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1
```

### Git Commands
```bash
# Create feature branch
git checkout -b feat/parallel-[operation-name]

# Commit with linked documentation
git commit -m "feat(cidx): parallelize [operation]

Results: [brief metrics]
See: docs/features/parallel-[operation].md"

# Merge back to cidx
git checkout feature/cidx
git merge --no-ff feat/parallel-[operation-name]
```

---

## Further Reading

- **CLAUDE.md**: Project overview and parallelization strategy
- **docs/memory_optimization_analysis.md**: Memory architecture details
- **Lace Documentation**: (in sylvan/src/lace/)
- **Sylvan Paper**: [If available in repo]
