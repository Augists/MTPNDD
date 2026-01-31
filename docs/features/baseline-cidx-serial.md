# Feature: Baseline Performance - feature/cidx Serial Implementation

## Problem Statement

This document establishes the performance baseline for the feature/cidx branch before implementing parallelization. The feature/cidx branch contains:
- Array-based edge storage (from feature/index)
- Serial MTPNDD operations (no parallelization yet)
- Optimized data structures for memory efficiency

This baseline will be used to measure the impact of parallel implementation techniques ported from feature/c.

## Baseline Metrics (N=12, Workers=1)

### Test Configuration
- **Branch**: feature/cidx
- **Test**: N-Queens N=12
- **Workers**: 1
- **Build flags**: `-DMTPNDD_ENABLE_RECORDING=ON`
- **Date**: 2026-01-31

### Performance Results

#### Baseline Set Sizes Configuration
```
experiment	init	declare	build	sat(to_mtbdd+satcount)	total	java_like(declare+build)	nodes
baseline_set_sizes	0.176	0.001	6.190	0.321	6.687	6.190	1852552
```

**Key Metrics**:
- **Total time**: 6.687 seconds
- **Build time**: 6.190 seconds (92.6% of total)
- **Saturation count**: 0.321 seconds
- **Final nodes**: 1,852,552 nodes

**Build Breakdown**:
- `row_or`: 0.018s (144 ops)
- `row_and`: 0.000s (12 ops)
- `imp_or`: 0.009s (5,192 ops)
- `imp_and`: 0.029s (5,192 ops)
- `cell_and`: 6.133s (144 ops) - **91.8% of build time**

**Per-Cell Statistics**:
- Formula AND average: 42.590 ms
- Formula AND p50: 22.716 ms
- Formula AND p90: 118.692 ms
- Formula AND max: 134.293 ms

#### Small Growth Configuration
```
small_growth_set_sizes	0.001	0.000	7.363	0.320	7.683	7.363	487123
```

**Key Metrics**:
- **Total time**: 7.683 seconds
- **Build time**: 7.363 seconds
- **Final nodes**: 487,123 nodes (73.7% reduction from baseline)

### Comparison Target: feature/c

From performance analysis documents, feature/c achieved:
- **Worker=1**: 11.805 seconds (serial, hashmap-based)
- **Initial (before optimization)**: 61.8 seconds

**Expected Gap**:
The feature/cidx serial baseline (6.687s) is **significantly faster** than feature/c Worker=1 (11.805s). This suggests:
1. Array-based edge storage provides better cache locality
2. Index-based node representation is more efficient
3. The parallelization techniques from feature/c should build on this strong foundation

## Analysis

### Bottleneck Identification

**cell_and dominates execution time**:
- 6.133s out of 6.190s build time (99.1%)
- 144 operations with average 42.590ms each
- High variance: max (134.293ms) is 5.9x the average

**Implication for Parallelization**:
- AND operation is the primary target for parallelization
- Large variance in AND operation times suggests opportunity for work-stealing
- Each cell_and involves processing many edge pairs (data parallelism opportunity)

### Memory Usage

**Note**: Memory profiling not performed in this baseline run. Will be captured during Phase 1 testing.

### Expected Parallelization Impact

Based on feature/c experience:
1. **Batched SPAWN/SYNC** (Phase 1): May show speedup at higher worker counts
2. **Per-Worker Pools** (Phase 2): Should reduce allocation overhead (7.8% improvement in feature/c)
3. **Nodetable Locks** (Phase 3): Required for correctness with multiple workers
4. **Lock-free Cache** (Phase 4): Should reduce cache contention

**Conservative Goal**: Maintain W=1 performance (~6.7s) while enabling scalability at W=4+

**Optimistic Goal**: Achieve better than linear speedup due to improved cache behavior

## Next Steps

1. **Phase 1**: Implement Batched SPAWN/SYNC pattern for mtpndd_and operation
2. Measure performance with W=1, W=2, W=4
3. Compare against this baseline

## Verification

**Solutions verified**: 14,200 solutions for N=12 (correct)

**Baseline established**: ✅

---

**Document created**: 2026-01-31
**Baseline file**: `/tmp/baseline-cidx-serial.txt`
