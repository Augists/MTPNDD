# Plot-ready data (MTPNDD contention study)

## N-Queens runtime (program output)
### N=8
| workers | time(s) | solutions |
|---:|---:|---:|
| 1 | 0.094 | 92 |
| 2 | 0.060 | 92 |
| 3 | 0.057 | 92 |
| 4 | 0.057 | 92 |

### N=12
| workers | time(s) | solutions |
|---:|---:|---:|
| 1 | 12.128 | 14200 |
| 2 | 9.359 | 14200 |
| 3 | 8.455 | 14200 |
| 4 | 8.308 | 14200 |

### N=12 (slab pool tune A: refill=128, local_max=1024)
| workers | time(s) | solutions |
|---:|---:|---:|
| 1 | 17.841 | 14200 |
| 2 | 12.942 | 14200 |
| 3 | 10.846 | 14200 |
| 4 | 10.172 | 14200 |

### N=12 (slab pool tune B: edge_map/edge_entry=64/512, others=32/256)
| workers | time(s) | solutions |
|---:|---:|---:|
| 1 | 11.664 | 14200 |
| 2 | 9.017 | 14200 |
| 3 | 8.127 | 14200 |
| 4 | 7.907 | 14200 |

### N=12 (slab pool tune C: edge pools scale with workers)
| workers | time(s) | solutions |
|---:|---:|---:|
| 1 | 11.742 | 14200 |
| 2 | 8.976 | 14200 |
| 3 | 7.845 | 14200 |
| 4 | 7.943 | 14200 |

## perf stat (N=12, taskset 1–4 workers)
Units: cycles/instructions in G, cache-misses in M.
| workers | elapsed(s) | cycles(G) | instructions(G) | cache-misses(M) |
|---:|---:|---:|---:|---:|
| 1 | 12.347 | 45.03 | 52.66 | 338.5 |
| 2 | 9.381 | 61.06 | 55.23 | 365.1 |
| 3 | 8.656 | 75.42 | 55.50 | 372.2 |
| 4 | 8.525 | 92.36 | 55.78 | 367.9 |

## strace -c futex (N=12)
| workers | futex calls | futex time(s) | usec/call | errors |
|---:|---:|---:|---:|---:|
| 1 | 2 | 0.000002 | 1 | 0 |
| 2 | 35,779 | 0.071044 | 1 | 3,801 |
| 3 | 57,922 | 0.238981 | 4 | 6,581 |
| 4 | 75,069 | 0.432329 | 5 | 8,686 |

## strace -tt futex detail (N=12, 4 workers)
| metric | value |
|---|---:|
| total futex calls | 73,896 |
| FUTEX_WAKE_PRIVATE | 45,368 |
| FUTEX_WAIT_PRIVATE | 28,528 |
| EAGAIN (WAIT) | 43 |

## perf futex tracepoint (N=12, 4 workers)
### Top uaddr sample shares
| uaddr | sample share (%) |
|---|---:|
| 0x5601809db6f0 | 59.70 |
| 0x5601809db5f0 | 35.24 |
| 0x5601809db670 | 1.73 |
| 0x5601809db570 | 1.60 |

### Op type shares
| op | sample share (%) |
|---|---:|
| 0x00000081 (WAKE) | 59.90 |
| 0x00000080 (WAIT) | 38.38 |

## Futex address mapping (ASLR-robust)
| futex uaddr (low 12 bits) | mapped pool lock |
|---|---|
| 0x6f0 | g_edge_map_pool.lock |
| 0x5f0 | g_edge_entry_pool.lock |
| 0x670 | g_nodetable_entry_pool.lock |
| 0x570 | g_node_pool.lock |

Notes: mapping derivation is in `docs/archived/perf/futex_addr_mapping.md`.

## perf futex pool-lock sample share (N=12, 4 workers)
Derived by mapping uaddr sample shares to pool locks.
| pool lock | sample share (%) |
|---|---:|
| g_edge_map_pool.lock | 59.70 |
| g_edge_entry_pool.lock | 35.24 |
| g_nodetable_entry_pool.lock | 1.73 |
| g_node_pool.lock | 1.60 |

## perf c2c intensity metrics (N=12, 4 workers)
| metric | value |
|---|---:|
| total records | 209,626 |
| load operations | 103,093 |
| store operations | 106,533 |
| load local HITM | 1,238 |
| locked load/store ops | 4,869 |
| total shared cache lines | 41 |
| total merged records | 12,850 |
| locked access on shared lines | 4,028 |
| top cacheline address | 0x5632175b6b00 |
| top cacheline HITM | 970 |
| top cacheline HITM share | 78.35% |
| HITM per load | 1.20% |
| HITM per store | 1.16% |
| locked shared access ratio | 31.35% |

## refs_stats instrumentation (N=12, SYLVAN_REFS_STATS=ON)
| workers | modify_calls | ups | downs | updates | misses | retries | avg_probes |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 50,735,424 | 27,008,577 | 23,726,847 | 50,735,421 | 8,999 | 0 | 1.091 |
| 4 | 50,764,202 | 27,022,966 | 23,741,236 | 50,764,198 | 9,020 | 78,844 | 1.089 |

Top bucket_mod distributions are recorded in `docs/archived/perf/refs_stats_n12.md`.
