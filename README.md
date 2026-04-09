# MTPNDD

**Multi-Terminal Parallel Network Decision Diagrams** — a high-performance C library for parallel NDD operations, built on [Sylvan](https://github.com/trolando/sylvan) (BDD) and [Lace](https://github.com/trolando/lace) (work-stealing parallelism), with a Java API via JNI.

## Architecture

![architecture](docs/mtpndd_architecture.png)

MTPNDD extends Sylvan's BDD layer with multi-terminal NDD nodes. The stack is:

| Layer | Component | Role |
|-------|-----------|------|
| **Java API** | `MTPNDDEngine` / `MTPNDD` / `MTPNDDConfig` | High-level interface for Java applications |
| **JNI Bridge** | `mtpndd_jni.c` | Maps Java calls to native C API |
| **C API** | `mtpndd.h` | Single-include header exposing all operations |
| **Core** | `mtpndd_node`, `mtpndd_nodetable`, `mtpndd_operation_cache`, `mtpndd_memory_pool` | Node operations, canonicalization, caching, slab memory pools |
| **Runtime** | Sylvan + Lace | BDD storage/ops + work-stealing task scheduler |

## Key Features

- **Parallel boolean operations** — AND, OR, NOT, DIFF, EXIST with task-level parallelism via Lace SPAWN/SYNC
- **Two-phase AND** — filter-then-recurse strategy that prunes false edge pairs before spawning recursive tasks
- **Slab memory pools** — batch-allocated object pools for nodes, edges, and table entries; eliminates malloc/free contention
- **Lock-free operation cache** — lock-striped result caching for AND/OR/NOT
- **Dynamic nodetable** — spinlock-sharded buckets with automatic rehash and GC-triggered growth
- **DOT visualization** — export any NDD subgraph to Graphviz DOT format
- **Java API** — full-featured JNI bridge with builder-pattern configuration

## Quick Start

```bash
# Build native library
cd sylvan
cmake -B build -DMTPNDD_LOG_LEVEL=0
cmake --build build

# Run N-Queens smoke test
./build/src/sylvan/mtpndd/mtpndd_nqueens_test 8
```

For full build instructions, JNI setup, configuration reference, and more, see the **[Wiki](../../wiki)**.

## Performance

N-Queens benchmark (6 workers, `MTPNDD_LOG_LEVEL=0`):

| N | 1 worker | 6 workers | Speedup | Solutions |
|---|----------|-----------|---------|-----------|
| 10 | 0.489s | 0.154s | **3.18x** | 724 |
| 11 | 2.329s | 0.629s | **3.70x** | 2,680 |
| 12 | 12.377s | 3.114s | **3.98x** | 14,200 |
| 13 | 75.547s | 17.617s | **4.29x** | 73,712 |

Near-linear scaling on large problem sizes — 4.29x speedup with 6 workers at N=13.

## License

Apache-2.0 — see [LICENSE](LICENSE).
