# Feature: [Feature Name]

**Status**: [In Progress / Complete]
**Branch**: feature/[name]
**Date Started**: [YYYY-MM-DD]
**Date Completed**: [YYYY-MM-DD]

---

## Problem Statement

### Current Limitation
What is the current bottleneck or limitation?
- Example: "mtpndd_and operation executes sequentially, wasting parallelism"

### Business Case
Why does this matter?
- Example: "AND is called millions of times in N-Queens, dominating execution time"

---

## Analysis

### Current Implementation Flow

```
[Pseudocode or ASCII diagram of current serial implementation]

Example for mtpndd_and:
1. Check operation cache
2. Handle terminal cases
3. Get variable order and cofactors
4. left = AND(a_low, b_low)    <- SLOW SEQUENTIAL
5. right = AND(a_high, b_high) <- SLOW SEQUENTIAL
6. Make node with (left, right)
```

### Parallelization Strategy

**Approach**: [Describe high-level strategy]

Example for AND:
- Spawn low branch as Lace task
- Execute high branch in current worker
- Synchronize before combining results

**Synchronization Needs**:
- [ ] Operation cache: Lock-free CAS / Locked / None?
- [ ] Node table: Global lock / Per-bucket / Atomic?
- [ ] Edge pool: Atomic append / Per-worker buffer?

**Expected Impact**:
- **Time**: 2-3x speedup on 4 workers
- **Memory**: No expected change (< 2% acceptable)
- **Complexity**: Medium (Lace tasks + synchronization)

**Risk Assessment**:
- Where could race conditions occur?
- What about deadlocks?
- Performance degradation risks?

---

## Implementation

### Code Changes Summary

| File | Lines | Change Description |
|------|-------|---------------------|
| mtpndd_node.c | 1150-1200 | Parallelize mtpndd_and operation |
| mtpndd_node.h | 45 | Add parallel variant declaration |
| mtpndd_operation_cache.c | 120-135 | Add atomic CAS for cache insertion |

### Key Functions Modified

#### 1. mtpndd_and (or equivalent)
```c
// Before: Sequential execution
mtpndd_t left = mtpndd_and(a_low, b_low);
mtpndd_t right = mtpndd_and(a_high, b_high);
mtpndd_t result = mtpndd_makenode(v, left, right);

// After: Parallel execution
LACE_NEWFRAME();
mtpndd_t left;
lace_spawn_task(task_and_low, &left, a_low, b_low);

mtpndd_t right = mtpndd_and_parallel(a_high, b_high);

lace_yield(LACE_SYNC_ALL);

mtpndd_t result = mtpndd_makenode(v, left, right);
```

### New Data Structures or Primitives

- None added to core structures (reuse existing)
- New Lace task functions: `task_and_low()`, etc.
- No new synchronization primitives needed (use Lace atomics)

### Integration Points

**With Garbage Collection**:
- How does GC interact with parallel operations?
- Are temp_refs properly managed?

**With Other Operations**:
- Does this parallelize well alongside other parallel operations?
- Cache contention issues?

---

## Testing

### Test Environment

- **Benchmark Scenario**: nqueens N=12
- **Baseline Reference**: [link to feature/index baseline or previous feature]
- **Build Configuration**: `cmake -B build && cmake --build build`
- **Test Command**: `./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp [N] [workers]`

### Correctness Testing

- **Single Worker** (`-w 1`)
  ```bash
  ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_test 8      # PASS?
  ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1    # Results match?
  ```

- **Multi-Worker** (`-w 4`)
  ```bash
  ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 4    # Results match single-worker?
  ```

- **Memory Safety**
  ```bash
  valgrind --leak-check=full ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1
  ```
  Result: [No errors/leaks? ✓ or ✗]

### Performance Testing Results

#### Metrics (N=12 nqueens)

| Metric | Baseline | After Feature | Change | Status |
|--------|----------|---------------|--------|--------|
| Peak RSS | [X] MB | [Y] MB | [+/-]Z% | ✅/⚠️/❌ |
| Peak VMS | [X] MB | [Y] MB | [+/-]Z% | ✅/⚠️/❌ |
| Time (-w1) | [X]s | [Y]s | [+/-]Z% | ✅/⚠️ |
| Time (-w2) | [X]s | [Y]s | [+/-]Z% | ✅ |
| Time (-w4) | [X]s | [Y]s | [+/-]Z% | ✅ |

#### Detailed Breakdown (if instrumentation enabled)

| Operation | Baseline | Optimized | Speedup | Notes |
|-----------|----------|-----------|---------|-------|
| AND | [X]s | [Y]s | [Z]x | |
| OR | [X]s | [Y]s | [Z]x | |
| Other | [X]s | [Y]s | [Z]x | |

#### Analysis

- ✅ Correctness: Results match baseline
- ✅ Performance: Speedup [Z]x on [W] workers ([E]% efficiency)
- ⚠️ Single-worker: [+/-]Z% (acceptable/concerning because...)
- ✅ Memory: [+/-]Z% (within acceptable range)

#### Reproducibility

Tested [N] times with consistent results? [Yes/No]
- Run 1: [time]
- Run 2: [time]
- Run 3: [time]
- Standard deviation: [X]%

---

## Unexpected Findings

### Finding 1: [Description]
**Impact**: [Severity]
**Explanation**: [Why did this happen?]
**Resolution**: [How was it addressed?]

### Finding 2: [Description]
[etc.]

---

## Lessons & Analysis

### What Worked Well

1. **[Aspect 1]** - [Why was this successful?]
   - Example: "Spawning one branch and executing other in current worker balanced load efficiently"

2. **[Aspect 2]**
   - Example: "Lace task scheduling handled dynamic load well"

### Challenges Encountered

1. **[Challenge 1]** - [Description and solution]
   - Example: "Single-worker case regressed by 2% due to task frame overhead"
   - Solution: "Consider adding threshold to skip parallelization for tiny tasks"

2. **[Challenge 2]**
   - [etc.]

### Trade-offs Made

1. **[Trade-off 1]**: Chose [Option A] over [Option B]
   - Why: [Rationale]
   - Cost: [What do we lose?]
   - Benefit: [What do we gain?]

### Recommendations for Next Steps

1. **[Short-term]** - [What to do immediately?]
   - Example: "Parallelize mtpndd_or using same pattern"

2. **[Medium-term]** - [Future optimizations?]
   - Example: "Investigate task granularity threshold optimization"

3. **[Long-term]** - [Broader improvements?]
   - Example: "Profile memory contention under high parallelism"

---

## Related Commits

| Commit Hash | Message | Status |
|-------------|---------|--------|
| [abc1234] | [feat(cidx): implement feature] | ✓ Merged |
| [def5678] | [test: add verification] | ✓ Merged |
| [ghi9012] | [docs: finalize feature doc] | ✓ Merged |

**Final commit linking to this document**:
```
feat(cidx): [feature name]

- Implemented [key changes]
- Results: [brief summary]
- See: docs/features/[this-file].md

Co-Authored-By: [Author] <[email]>
```

---

## Appendix: Raw Measurement Data

### Baseline Metrics

```
[Full output from measure_memory.sh for baseline]
```

### Optimized Metrics

```
[Full output from measure_memory.sh for optimized version]
```

### Build Logs

```
[Any relevant build or compilation information]
```

---

## References

- **CLAUDE.md**: Project overview
- **WORKFLOW_GUIDE.md**: Detailed development workflow
- **Feature/index baseline**: [Link to previous feature document]
- **Lace Framework**: sylvan/src/lace/lace.h
- **Sylvan Documentation**: [If available]
