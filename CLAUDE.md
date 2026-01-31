# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 📚 Documentation Entry Points

**Start here**:
- **[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)** - Complete documentation map and navigation guide
- **[docs/QUICK_START_CIDX.md](docs/QUICK_START_CIDX.md)** - 10-minute overview and 4-5 hour feature cycle
- **[docs/WORKFLOW_GUIDE.md](docs/WORKFLOW_GUIDE.md)** - Detailed step-by-step workflow (7 phases)

## Project Overview

**MTPNDD** (Multi-Terminal Parallel Network Decision Diagrams) is a C library for parallel decision diagram operations with Java/JNI bindings. It implements a novel idx-based representation for nodes with edge array pooling, enabling efficient construction of multi-terminal decision diagrams.

**Repository**: XJTU ANTS Netverify Lab
**Language**: C11 (core) + Java 11+ (bindings)
**Build System**: CMake + Maven
**License**: Apache-2.0

### Branch Strategy and Current Goal

**Branch Evolution**:
- **feature/serial**: Original hashmap-based implementation with serial MTPNDD logical operations
- **feature/index**: Optimized data structures (array-based instead of hashmap) while maintaining serial operation execution
- **feature/cidx** (current): Implement parallel execution of MTPNDD logical operations on top of feature/index's array-optimized structures

**Current Goal**: Parallelize MTPNDD logical operations (AND, OR, DIFF, EXIST, etc.) to maximize library efficiency while leveraging the memory-optimized data structures from feature/index.

**Key Insight**: feature/index achieved significant memory efficiency through structural optimization. Now feature/cidx focuses on **time efficiency** by parallelizing recursive operations using the Lace task framework, while maintaining the same optimized data structures.

## Building and Running

### MTPNDD C Library

```bash
# Production build
cd sylvan
cmake -B build
cmake --build build

# Development build with performance instrumentation
cmake -B build -DMTPNDD_ENABLE_RECORDING=ON -DMTPNDD_ENABLE_EDGE_STATS_FILE=ON
cmake --build build

# Run single test (N-Queens with board size 8)
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_test 8

# Run performance benchmark (N-Queens N=12 with 1 worker)
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1
```

### JNI Java Bindings

```bash
cd jni

# Build native library
cmake -B build
cmake --build build  # Produces libmtpnddjni.so

# Build and package with Maven
mvn clean package

# Run JUnit tests
mvn test

# Run single test class
mvn test -Dtest=MTPNDDTest
```

### Memory Profiling

```bash
# Fast /proc-based memory monitoring
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1

# Detailed Valgrind/Massif analysis (slower, more detailed)
./tools/measure_memory.sh massif ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1

# Batch comparison across multiple N values
./tools/memory_compare.sh [baseline_dir] [optimized_dir]
```

## Codebase Architecture

### Core MTPNDD Data Structures

**Location**: `sylvan/src/sylvan/mtpndd/`

The library uses an **idx-based representation** where nodes are uint64_t indices into a dynamic table:

1. **mtpndd_node.c/h** (1,181 lines)
   - Core operations: AND, OR, NOT, DIFF, EXIST, saturation count
   - BDD conversion (BDD → MTPNDD)
   - Implements recursive algorithms with memoization

2. **mtpndd_nodetable.c/h** (762 lines)
   - Node uniquification via open-addressing hash table (O(1) lookup)
   - Garbage collection with stop-the-world and edge pool compaction
   - Atomic GC hook integration with Sylvan

3. **mtpndd_edge_array_pool.c/h** (82 lines)
   - Append-only continuous storage for edges (most memory-intensive)
   - Optional compaction to reclaim fragmented space
   - Currently ~150 MB of 772 MB total for N-Queens N=12

4. **mtpndd_operation_cache.c/h** (203 lines)
   - 2-way memoization for AND/OR operations
   - Reduces redundant computation in recursive algorithms

5. **mtpndd_common.c/h** (637 lines)
   - Initialization, configuration, error handling
   - Thread-local worker lifecycle

6. **mtpndd_edge_builder.c/h** (136 lines)
   - Edge construction utilities for domain experts

7. **mtpndd_edge_stats.c/h** (91 lines)
   - Instrumentation for operation statistics collection
   - Enabled via `MTPNDD_ENABLE_RECORDING` CMake flag

### Java JNI Bridge

**Location**: `jni/src/main/java/org/ants/mtpndd/` and `jni/src/main/native/`

Key classes:
- **MTPNDDEngine**: Lifecycle management (init/shutdown), native bridging
- **MTPNDD**: Thin wrapper around uint64_t node indices
- **MTPNDDConfig**: Builder pattern for configuration
- **MTPNDDStats**: Statistics reporting
- **mtpndd_jni.c**: Native bridge implementation

**Typical Usage Pattern**:
```java
MTPNDDConfig config = MTPNDDConfig.builder()
    .workers(1)
    .mtpnddNodeTableSize(1 << 18)
    .operationCacheSize(1 << 18)
    .build();
MTPNDDEngine.init(config);

int fieldId = MTPNDDEngine.declareField(bitWidth);
MTPNDDEngine.generateFields();
MTPNDD result = var1.and(var2).or(var3);

MTPNDDEngine.shutdown();
```

### Embedded Sylvan Library & Lace Task Framework

**Location**: `sylvan/src/sylvan/`

Bundled parallel decision diagram library providing:
- BDD, MTBDD, LDD, ZDD implementations
- Lace task parallelization framework (bundled) - **critical for feature/cidx parallelization**
- Atomic GC hooks for integration with user code (MTPNDD uses this)

Current version: 1.9.1

#### Lace Framework (for Parallelization)

**Key Components** available for MTPNDD operations:
- **Work-stealing scheduler**: Distributes recursive tasks across workers
- **Task frames**: `lace_newframe()` / `lace_deleteframe()` - manage task recursion depth
- **Task spawning**: `lace_spawn_task()` - create concurrent subtasks
- **Synchronization**: Atomic operations, barriers, worker management
- **Worker threads**: Managed automatically, configured via `n_workers` in config

**Typical Parallelization Pattern**:
```c
// Serial recursive AND
mtpndd_t
mtpndd_and(mtpndd_t a, mtpndd_t b)
{
    // Check cache, handle terminals...
    mtpndd_t left = mtpndd_and(a_low, b_low);
    mtpndd_t right = mtpndd_and(a_high, b_high);
    return mtpndd_makenode(...);
}

// Parallel version using Lace
mtpndd_t
mtpndd_and_parallel(mtpndd_t a, mtpndd_t b)
{
    // Check cache, handle terminals...

    // Spawn left subtask
    LACE_NEWFRAME();
    mtpndd_t left;
    lace_spawn_task(task_and_low, &left, a_low, b_low);

    // Execute right in current worker
    mtpndd_t right = mtpndd_and_parallel(a_high, b_high);

    // Wait for left task
    lace_yield(LACE_SYNC_ALL);

    return mtpndd_makenode(...);
}
```

**Important**: Lace is already integrated in Sylvan and MTPNDD initialization sets up workers. Tasks should use the same temp_refs mechanism for memory safety during GC.

## Memory Architecture

### Baseline (N-Queens N=12)
- **Peak RSS**: 772.12 MB (physical memory)
- **Peak VMS**: 6158.89 MB (virtual memory)

### Breakdown
- Sylvan BDD structures: ~400-500 MB
- MTPNDD Edge Array Pool: ~120-150 MB (optimization target)
- MTPNDD Node Table: ~60-70 MB
- MTPNDD Hash Table: ~20-30 MB
- Other (stack, misc): ~100-120 MB

### Optimization Status

**Completed**:
- Replaced `gc_protect` with efficient `temp_refs` (+16.3% performance, `c9825e6`)
- Atomic GC hook integration (`0dfc72b`)
- Two-phase field generation with shared BDD variables (`25699be`)

**Documented Opportunities** (see `docs/memory_optimization_analysis.md`):
- Edge pool fragmentation compaction
- Hash table slot optimization
- Node record padding reduction
- Lazy edge materialization
- Dynamic resizing threshold tuning

**Conservative estimate**: 5-10% savings achievable; aggressive optimization could reach 30-60%.

## Configuration

All configuration is centralized in `mtpndd_pal_config_t`:

```c
config.n_workers = 1;                              // Thread count
config.lace_dqsize = 1 << 18;                      // Task parallelism queue

config.bdd_nodetable_size = 1 << 16;               // BDD node table (Sylvan)
config.op_cache_size = 1 << 12;                    // Sylvan operation cache

config.mtpndd_nodetable_size = 1 << 14;            // MTPNDD initial node table
config.mtpndd_nodetable_max_size = 1 << 20;        // Max node table size
config.nodetable_bucket_count = 1 << 12;           // Hash table initial buckets
config.nodetable_bucket_max_count = 1 << 20;       // Hash table max buckets
config.edge_entry_slab_capacity = 1 << 14;         // Edge pool initial capacity
config.quick_growth_threshold = 0.10;              // GC trigger at 10% capacity
```

These are initialized in `mtpndd_common.c` and exposed through Java builder in `MTPNDDConfig.java`.

## Testing

### C Test Suite
Located in `sylvan/src/sylvan/mtpndd/test/`:

- **mtpndd_nqueens_test**: Correctness validation (N=8 typical)
- **mtpndd_nqueens_exp**: Performance benchmarking with detailed metrics
  - Captures: operation times (by type), saturation count, cache hit rates, node/edge counts
- **mtpndd_conversion_test**: BDD ↔ MTPNDD conversion verification

### Java Test Suite
Located in `jni/src/test/java/org/ants/mtpndd/`:

- **MTPNDDTest**: Core API functionality (JUnit 5)
- **NQueensMTPNDD**: Benchmark harness
- **ManualNativeCheck**: Manual verification utility

Run with `mvn test` or `mvn test -Dtest=ClassName#methodName`.

## Key Concepts

### Node Representation
Nodes are **uint64_t indices** (not pointers). This enables:
- Fast table resizing without pointer updates
- Efficient GC with pool compaction
- Better cache locality in array storage

### Edge Array Pool
Edges are stored in a **continuous append-only pool** with optional compaction:
- Reduces fragmentation vs. malloc per edge
- Enables batch GC by discarding old pool slabs
- Currently the largest memory consumer (~150 MB for N=12)

### Operation Memoization
- 2-way cache for AND/OR results
- Keyed by (node1_idx, node2_idx) pairs
- Managed separately from Sylvan's operation cache

### Garbage Collection
- Stop-the-world with Sylvan atomic hook
- Compacts edge pool and rebuilds hash table
- Triggered when: GC request + quick_growth_threshold exceeded

## Important Files and Locations

| File | Purpose |
|------|---------|
| `sylvan/CMakeLists.txt` | Root CMake config with feature flags |
| `sylvan/src/sylvan/mtpndd/mtpndd.h` | Main public API header |
| `jni/pom.xml` | Maven configuration for Java build |
| `docs/memory_optimization_analysis.md` | Detailed memory breakdown and optimization paths |
| `docs/MEMORY_OPTIMIZATION_GUIDE.md` | Quick-start optimization checklist |
| `tools/measure_memory.sh` | Memory profiling tool (/proc or Valgrind) |
| `README.md` | Project overview, build steps, branch explanation |

## Development Workflow Guide

**For detailed step-by-step process**: See **docs/WORKFLOW_GUIDE.md** (complete workflow with all phases)

**For quick reference**: See **docs/QUICK_START_CIDX.md** (4-5 hour feature development cycle)

### Workflow Overview

Each optimization feature follows this structured process:

1. **Problem Definition** - Identify the performance bottleneck or synchronization issue
2. **Analysis** - Study existing code, design parallel strategy, plan implementation
3. **Code Implementation** - Implement feature with appropriate synchronization primitives
4. **Testing & Measurement** - Run baseline tests, compare metrics (memory/time)
5. **Documentation** - Record findings and results in feature-specific document
6. **Git Checkpoint** - Commit with structured message and link to documentation

### Feature Documentation Template

Each completed feature should have a dedicated document in `docs/features/` with:

```markdown
# Feature: [Feature Name]

## Problem Statement
- What bottleneck or limitation does this address?
- What are the current limitations?

## Analysis
- Design approach (which algorithm/pattern?)
- Synchronization strategy (locks, atomics, work-stealing?)
- Expected impact on memory/performance
- Risk assessment

## Implementation
- Key code changes
- Files modified (with line ranges)
- New data structures or synchronization primitives introduced
- Integration points with existing code

## Testing Results
- Baseline metrics (N=12 nqueens)
- Optimized metrics (N=12 nqueens)
- Comparison: memory change, time change, throughput
- Statistical significance (if applicable)

## Lessons & Analysis
- Did results match expectations? Why/why not?
- Unexpected findings?
- Trade-offs made (memory vs. speed, complexity vs. performance)?
- Recommendations for future work

## Related Commits
- List git commit hashes that implement this feature
```

### Test Baseline Setup

All measurements use **nqueens N=12** as the standard benchmark:

```bash
# Baseline measurement (feature/index or previous feature)
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1 > baseline.txt

# After implementing new feature
cmake -B build  # Rebuild with new changes
cmake --build build
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1 > optimized.txt

# Compare
./tools/memory_compare.sh baseline.txt optimized.txt
```

**Key Metrics to Track**:
- Peak RSS (physical memory)
- Peak VMS (virtual memory)
- Total execution time
- Build time breakdown (by operation type)
- Cache hit/miss rates (if instrumentation enabled)
- Node table size at completion
- Edge pool utilization

### Current Active Features (feature/cidx)

The feature/cidx branch focuses on **parallelizing logical operations** while leveraging array-optimized structures from feature/index:

#### Primary Parallelization Targets
1. **Recursive AND/OR operations** - Most time-consuming, highly parallelizable
2. **EXIST/FORALL operations** - Reduction over decision diagram structure
3. **Saturation counting** - Can use parallel reduction patterns
4. **Garbage collection** - Parallel edge pool scanning

#### Synchronization Challenges to Address
- **Operation cache consistency** - Multiple threads computing same subproblems
- **Node table mutations** - Concurrent node insertion during AND/OR
- **Edge pool contention** - Append-only writes from multiple workers
- **GC coordination** - Stop-the-world with worker quiescence

#### Integration with Lace
- Already available: Task queue, worker management, synchronization primitives
- Use: `lace_newframe()`, `lace_spawn_task()`, `lace_yield()` for work distribution
- Current code structure: Recursive functions that can spawn tasks at decision points

### Development Practices

#### Before Starting a Feature

1. Create a feature branch: `git checkout -b feature/[feature-name]`
2. Create feature document in `docs/features/[feature-name].md` (rough outline)
3. Identify baseline metrics from feature/index

#### During Implementation

1. Commit logically related changes frequently (don't accumulate huge commits)
2. Use commit messages: `feat(cidx): [feature name] - [specific change]`
3. Update feature document as you discover insights
4. Keep test builds incremental to catch issues early

#### After Feature Implementation

1. Ensure clean build: `cmake --build build --clean-first`
2. Run full test suite
3. Capture detailed metrics with `MTPNDD_ENABLE_RECORDING=ON`
4. Document all findings in feature document
5. Create final commit linking to documentation:
   ```
   feat(cidx): complete [feature name]

   - Implemented [key changes]
   - Results: [brief metric summary]
   - See: docs/features/[feature-name].md

   Co-Authored-By: Claude <noreply@anthropic.com>
   ```

#### Documentation Checklist

- [ ] Problem statement is clear
- [ ] Analysis explains design choices
- [ ] Code changes are traceable (files + line ranges)
- [ ] Test results are reproducible (include build flags, worker count)
- [ ] Before/after metrics are compared
- [ ] Unexpected findings are noted
- [ ] Next steps or recommendations are listed
- [ ] Feature commit hash is documented

### Important Caveats for Parallelization

#### Memory Consistency
- Ensure proper memory barriers when writing node/edge structures
- Be aware of false sharing in hash table buckets across workers

#### Work Balance
- Task parallelism from Lace handles load balancing
- But recursive AND/OR may produce unbalanced trees - monitor worker utilization

#### Debugging Parallel Code
- Use deterministic worker count (e.g., `-w 1` first to verify logic)
- Enable recording with `MTPNDD_ENABLE_RECORDING=ON` for operation traces
- Valgrind with `--tool=helgrind` to detect race conditions (slow)

#### Incremental Parallelization Strategy
- Start with one operation (e.g., AND) fully parallelized
- Measure and validate results
- Then parallelize other operations
- This reduces complexity and makes regressions visible immediately

## Branch Management

- **feature/serial**: Historical reference (hashmap serial implementation)
- **feature/index**: Stable baseline for feature/cidx (array-optimized, serial)
- **feature/cidx**: Active development (array-optimized + parallel operations)

When feature/cidx is complete and validated, it becomes the new baseline for future optimizations.

## Current Development Focus (feature/cidx)

### Immediate Goals (Phase 1: Foundation)

1. **Baseline measurements from feature/index**
   - Establish exact memory and time metrics
   - Document in `docs/features/baseline-feature-index.md`
   - All future comparisons use this as reference

2. **Parallelize mtpndd_and operation**
   - Most impactful operation (high frequency in nqueens)
   - Clear parallelization pattern
   - Good test of synchronization strategy
   - Target: 2-3x speedup on 4+ workers

3. **Validate synchronization approach**
   - Choose operation cache strategy (lock-free vs. locked)
   - Choose node table update strategy
   - Test for race conditions under load

### Medium-Term Goals (Phase 2: Expansion)

4. **Parallelize mtpndd_or operation**
   - Similar to AND; reuse synchronization patterns from Phase 1

5. **Parallelize mtpndd_satcount and reduction operations**
   - Different parallelization pattern (reduction vs. divide-and-conquer)
   - Good validation of framework generality

6. **Performance tuning**
   - Task granularity optimization (when to spawn vs. execute serially)
   - Worker pool sizing (determine optimal `-w` count)
   - Cache behavior analysis

### Long-Term Goals (Phase 3: Polish)

7. **Parallelize remaining operations** (DIFF, EXIST, FORALL)
8. **Memory optimization under parallelism** - Ensure no degradation
9. **Comprehensive benchmarking** - Extended test suite beyond nqueens
10. **Production readiness** - Performance regression tests, documentation

### Success Criteria

For each feature to be considered complete:
- ✅ Code compiles and runs without errors
- ✅ Results match serial version (correctness)
- ✅ Performance improvement > 5% over feature/index baseline (with `-w 4+`)
- ✅ No memory increase > 5% from baseline
- ✅ Feature documented in `docs/features/[name].md`
- ✅ Commits with clear messages linking to documentation
- ✅ All tests pass with deterministic results

## Compiler Flags and Dependencies

**C Compilation**:
- Standard: C11
- Flags: `-Wall -Wextra -Wno-unused-parameter`
- Requires: CMake 3.17+, pthreads

**Java Compilation**:
- Target: Java 11+
- Build tool: Maven 3.6+
- Test framework: JUnit 5

**Optional Dependencies**:
- Valgrind: Memory profiling
- Graphviz: DOT visualization generation

## Parallelization Strategy for feature/cidx

### Architecture Overview

**Serial (feature/index) → Parallel (feature/cidx)** transformation:

1. **Data structures remain unchanged** - Array-based node table, edge pool, etc.
2. **Operations become parallel-aware** - AND, OR, DIFF, EXIST spawn Lace tasks
3. **Caches need synchronization** - Operation cache becomes concurrent
4. **GC coordination changes** - Workers must quiesce before GC

### Identified Parallelization Opportunities

#### High Priority (Most Time-Critical)
1. **mtpndd_and / mtpndd_or** - Recursive operations with independent subtasks
   - Currently: Sequential evaluation of low and high branches
   - Parallelization: Spawn one branch as task, execute other in current worker
   - Expected speedup: 2-3x (limited by task scheduling overhead)

2. **mtpndd_satcount** - Reduction over all nodes
   - Currently: Sequential traversal
   - Parallelization: Parallel reduction with atomic accumulation
   - Expected speedup: Near-linear (reduction is associative)

#### Medium Priority (Good Parallelization, Moderate Impact)
3. **mtpndd_exist / mtpndd_forall** - Similar pattern to AND/OR
4. **mtpndd_diff** - Three-way branch could parallelize well

#### Lower Priority (High Synchronization Cost)
5. **Edge pool management** - Append-only; contention may exceed benefit
6. **Node table expansion** - Rare operation; not worth complex parallel logic

### Synchronization Patterns

#### Operation Cache
**Challenge**: Multiple workers may compute the same AND(x,y) simultaneously
**Solutions**:
- Lock-free approach: Use atomic CAS to insert result (faster)
- Lock-based approach: Mutex per cache bucket (simpler, slower)
- Hybrid: Reader-writer lock for cache (if lookups >> inserts)

**Current approach in MTPNDD**: 2-way cache with simple insertion - candidates for atomic update

#### Node Table
**Challenge**: Concurrent mtpndd_makenode() calls from multiple workers
**Current design**: Open-addressing hash table with existing uniqueness guarantees
**Parallelization**: Use atomic operations for bucket updates or per-bucket locks

#### Edge Pool
**Challenge**: append() from multiple workers
**Current design**: Append-only with optional compaction
**Parallelization**: Atomic append with allocation buffer per worker (reduces contention)

### Testing Strategy for Parallel Code

1. **Correctness First** - Test with `-w 1` (single worker) to isolate logic bugs
2. **Determinism** - Fixed worker count and seed for reproducible races
3. **Stress Testing** - High worker count with memory profiling
4. **Race Detection** - Optional Valgrind helgrind run (very slow)

### Red Flags When Parallelizing

- Correctness changes when using multiple workers
- Memory corruption or segfaults under high parallelism
- Performance degrades instead of improves
- Memory usage increases unexpectedly (extra task frames, buffers)
- Cache hit rate drops significantly (indicates false sharing or poor locality)

If any red flag occurs, revert the parallelization for that operation and investigate the synchronization strategy.

## Common Development Tasks

### Parallelizing an Existing Operation

**Checklist for converting a serial operation to parallel**:

1. **Understand current flow** - Trace the operation (e.g., mtpndd_and)
   - Identify independent subtasks (e.g., low/high branches)
   - Measure time spent on each (use `MTPNDD_ENABLE_RECORDING`)

2. **Design parallel strategy**
   - Which subtasks can run in parallel?
   - Which synchronization primitives are needed?
   - Document in feature document before coding

3. **Implement with Lace**
   ```c
   LACE_NEWFRAME();
   // Spawn task for one branch
   task_t t = lace_spawn_task(parallel_subtask, arg1, arg2);
   // Execute other branch in current worker
   result_main = serial_path();
   // Synchronize
   lace_yield(LACE_SYNC_ALL);
   result_spawned = (result from task);
   ```

4. **Test incrementally**
   - Build and run with `-w 1` (single worker)
   - Verify results match serial version
   - Increase worker count and re-test
   - Measure performance improvement

5. **Update documentation** - Add implementation notes to feature document

### Adding a New Serial Operation (if needed)

1. Implement in `mtpndd_node.c` following existing pattern
2. Add extern declaration in `mtpndd_node.h`
3. Add instrumentation (if `MTPNDD_ENABLE_RECORDING`)
4. Expose through JNI in `mtpndd_jni.c` (if Java API needed)
5. Add Java wrapper method in `MTPNDD.java`
6. Add test in `MTPNDDTest.java`
7. (Later) Parallelize using Lace pattern above

### Profiling Memory Usage
```bash
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1
# Compare baseline vs. optimization:
./tools/memory_compare.sh baseline_dir optimized_dir
```

### Enabling Instrumentation
```bash
cmake -B build -DMTPNDD_ENABLE_RECORDING=ON
cmake --build build
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1  # Will output statistics
```

### Debugging N-Queens Execution
- Trace operations: Check output metrics from `mtpndd_nqueens_exp`
- Verify correctness: Run `mtpndd_nqueens_test` with smaller N values
- Profile specific operations: Use `MTPNDD_ENABLE_RECORDING` flag
