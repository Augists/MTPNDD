# Phase 1: Batched SPAWN/SYNC Implementation

## Status: PARTIALLY IMPLEMENTED - HAS CORRECTNESS BUG

**Date**: 2026-01-31
**Branch**: feature/cidx

## Problem Statement

Implement parallel execution for `mtpndd_and` operation using the Batched SPAWN/SYNC pattern from feature/c. This is Phase 1 of the parallelization plan to improve MTPNDD performance while maintaining the array-based data structures from feature/index.

## Implementation Approach

### Architecture Overview

The implementation follows the feature/c pattern but adapted for array-based edges:

1. **Item Tasks**: Small units of work that compute one (child, label) pair
   - `mtpndd_and_same_field_item`: For same-field AND operations
   - `mtpndd_and_diff_field_item`: For different-field AND operations

2. **Batched SPAWN/SYNC**: Prevent task queue overflow
   - SPAWN multiple tasks up to a threshold (256)
   - Periodically drain half the pending tasks
   - Final drain before creating result node

3. **Should Spawn Heuristic**: Decides when to parallelize
   - Never spawn with 1 worker
   - Never spawn for terminal nodes
   - Spawn for top-level nodes (field_id <= 2)
   - Spawn when edge product >= 64

### Key Adaptations from feature/c

**Data Structure Differences**:
- feature/c: `edge_bucket_entry_t*` (linked list pointers)
- feature/cidx: Pass `(mtpndd_t node, uint32_t edge_index)` pairs
- Item tasks call `mtpndd_node_edge(node, index)` to fetch edge data

**Reference Counting**:
- Added `mtpndd_temp_refs_push_owned()` to avoid double-refing
- Item tasks return already-ref'd nodes
- Merge function uses `_owned` variant to transfer ownership

## Code Changes

###Files Modified

1. **sylvan/src/sylvan/mtpndd/mtpndd_node.c** (~450 lines changed)
   - Added item task structures and TASK declarations (lines 86-128)
   - Implemented item tasks (lines 131-197)
   - Implemented helper functions (lines 200-272)
   - Converted mtpndd_and_rec to parallel TASK (lines 277-444)
   - Updated public wrapper mtpndd_and (line 814)
   - Updated mtpndd_diff to call TASK (line 862)

### Key Implementation Details

#### Item Task (Same Field)
```c
TASK_IMPL_4(mtpndd_and_item_t, mtpndd_and_same_field_item,
            mtpndd_t, a, uint32_t, ia,
            mtpndd_t, b, uint32_t, ib)
{
    // Read edges fresh (safe across GC)
    const mtpndd_edge_record_t ea = mtpndd_node_edge(a, ia);
    const mtpndd_edge_record_t eb = mtpndd_node_edge(b, ib);

    // Compute combined label
    mtpndd_bdd_t combined_label = sylvan_ref(sylvan_and(ea.label, eb.label));
    if (combined_label == sylvan_false) {
        sylvan_deref(combined_label);
        return out;  // emit=0
    }

    // Recursive AND (parallel)
    mtpndd_t sub_result = mtpndd_and_rec_CALL(__lace_worker, __lace_dq_head,
                                               ea.child, eb.child);

    // Hold ref for caller
    mtpndd_ref(sub_result);

    out.emit = 1;
    out.child = sub_result;
    out.label = combined_label;
    return out;
}
```

#### Main AND Loop (Same Field)
```c
for (uint32_t ia = 0; ia < na.edge_num; ++ia) {
    for (uint32_t ib = 0; ib < nb.edge_num; ++ib) {
        const mtpndd_edge_record_t ea = mtpndd_node_edge(a, ia);
        const mtpndd_edge_record_t eb = mtpndd_node_edge(b, ib);

        if (mtpndd_should_spawn(ea.child, eb.child)) {
            // SPAWN for parallel execution
            mtpndd_and_same_field_item_SPAWN(__lace_worker, __lace_dq_head,
                                              a, ia, b, ib);
            __lace_dq_head++;
            pending++;
        } else {
            // Direct CALL for small problems
            mtpndd_and_item_t item = mtpndd_and_same_field_item_CALL(...);
            status = mtpndd_and_merge_item(&builder, &temp_refs, &item);
            if (status != MTPNDD_SUCCESS) goto fail_and;
        }

        // Periodic flush to prevent queue overflow
        if (pending >= MTPNDD_AND_PENDING_FLUSH_THRESHOLD) {
            size_t to_drain = pending / 2;
            status = mtpndd_and_drain_same_field_items(..., to_drain, ...);
            if (status != MTPNDD_SUCCESS) goto fail_and;
        }
    }
}

// Final drain
if (pending > 0) {
    status = mtpndd_and_drain_same_field_items(..., pending, ...);
    if (status != MTPNDD_SUCCESS) goto fail_and;
}
```

## Testing Results

### Build Status
- ✅ Code compiles without warnings
- ✅ All dependencies resolved correctly
- ✅ Lace TASK macros work properly

### Functional Testing

#### Basic Operations (PASS)
```bash
./test_and  # Custom simple test
Output: TRUE AND TRUE = TRUE OK
        FALSE AND TRUE = FALSE OK
```

#### Conversion Test (PASS)
```bash
./mtpndd_conversion_test
Output: >> IP set roundtrip ok
        >> union/intersection/difference ok
```

#### N-Queens Tests (FAIL - Correctness Bug)
```bash
./mtpndd_nqueens_test 4
Output: solutions=2 (expected 2) ✅ CORRECT

./mtpndd_nqueens_test 5
Output: solutions=7 (expected 10) ❌ WRONG

./mtpndd_nqueens_test 6
Output: solutions=3 (expected 4) ❌ WRONG (with forced serial)
        solutions=260 (expected 4) ❌ WRONG (with parallel enabled)

./mtpndd_nqueens_test 8
Output: Core dump / timeout

./mtpndd_nqueens_test 12
Output: Not attempted due to smaller test failures
```

### Bug Analysis

**Symptoms**:
1. Simple AND operations work correctly
2. N=4 nqueens works correctly (2 solutions)
3. N>=5 produces wrong solution counts
4. Bug persists even with `should_spawn` forced to return `false` (pure serial execution)
5. This indicates the bug is in the item task logic, NOT the parallelization framework

**Suspected Root Causes** (prioritized for investigation):

1. **Edge/Label Lifecycle Issue**
   - Item tasks might be incorrectly managing Sylvan BDD label refs
   - `mtpndd_edge_builder_push` takes ownership of label
   - Potential double-deref or missed ref somewhere

2. **Recursive Call Context**
   - Old code: `mtpndd_and_rec(ea.child, eb.child, temp_refs)`
   - New code: `mtpndd_and_rec_CALL(__lace_worker, __lace_dq_head, ea.child, eb.child)`
   - temp_refs is now local to each call instead of threaded through
   - Might affect GC protection timing

3. **Field ID Calculation**
   - Change from: `mtpndd_mk(mtpndd_node_read(a).field_id, &builder)`
   - To: `mtpndd_mk(result_field_id, &builder)` where result_field_id captured before loops
   - Logic: `(na.field_id == nb.field_id) ? na.field_id : min(na.field_id, nb.field_id)`
   - Potential off-by-one or incorrect field selection

4. **Merge Logic Difference**
   - Old: Inline push to builder inside loop
   - New: Item returns (child, label), merge happens later
   - Timing difference might affect some edge case

**Debugging Steps Needed**:

1. Add detailed logging to item tasks to trace every edge computation
2. Compare serial execution trace with original serial code
3. Check if mtpndd_mk is getting the same inputs in both versions
4. Verify edge_builder contains same edges before finalization
5. Check if any edges are being dropped during drain/merge

## Performance Results

**Not measured** - correctness must be fixed first before performance testing.

Expected metrics after fixing:
- W=1: ~6.7s (baseline from Phase 0)
- W=4: TBD (initial parallelization, may not show speedup yet)

## Known Issues

### Critical Issues (Blocking)

1. **Correctness Bug**: N-Queens produces wrong results for N>=5
   - Impact: Cannot proceed to performance testing
   - Priority: **CRITICAL**
   - Next Steps: Systematic comparison of execution traces

2. **Core Dumps for N>=8**
   - Likely consequence of correctness bug
   - May also indicate memory corruption

### Design Questions

1. **Should item tasks read edges fresh vs. pass edge data?**
   - Current: Pass (node, index), read fresh
   - Alternative: Pass edge data directly (copy on SPAWN)
   - Trade-off: Safety vs. performance

2. **Is temp_refs management correct across TASK boundaries?**
   - Each TASK call has its own temp_refs
   - Old code threaded temp_refs through recursion
   - Need to verify GC protection is still adequate

## Next Steps

### Immediate (Fix Correctness)

1. **Debug item task logic** (Est: 2-3 hours)
   - Add comprehensive logging
   - Compare traces with original serial code
   - Identify exact point where results diverge

2. **Fix root cause** (Est: 1-2 hours)
   - Once identified, implement fix
   - Verify with extensive testing

3. **Regression testing** (Est: 30 min)
   - Test N=4,5,6,8,10,12
   - Verify all produce correct results
   - Check memory usage is stable

### After Correctness Fix

4. **Performance baseline** (Est: 1 hour)
   - Measure W=1 performance (should match Phase 0 ~6.7s)
   - Test W=2, W=4 to see initial parallelization effect
   - Profile to identify any unexpected overhead

5. **Document and commit** (Est: 30 min)
   - Update this document with results
   - Create clean commit with working Phase 1
   - Prepare for Phase 2

## References

- **Original serial code**: git show feature/cidx:sylvan/src/sylvan/mtpndd/mtpndd_node.c
- **Feature/c parallel implementation**: git show origin/feature/c:sylvan/src/sylvan/mtpndd/mtpndd_node.c
- **Lace documentation**: sylvan/src/lace/lace.h
- **Phase 0 baseline**: docs/features/baseline-cidx-serial.md

## Conclusion

Phase 1 implementation is **structurally complete** but has a **critical correctness bug** that must be resolved before proceeding. The parallelization infrastructure is in place and compiles/runs, but produces wrong results for complex formulas.

**Recommendation**: Pause parallelization work and focus on debugging the correctness issue. Once fixed, Phase 1 can serve as a solid foundation for Phases 2-5.

**Current Code State**: Saved to `/tmp/mtpndd_node_parallel.c` and restored to working directory for continued debugging.

---

**Last Updated**: 2026-01-31
**Status**: BLOCKED on correctness bug
**Next Reviewer**: Should focus on comparing item task execution with original serial logic
