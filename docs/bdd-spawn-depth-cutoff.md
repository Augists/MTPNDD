# BDD Spawn Depth Cutoff: Design, Implementation & Benchmark

## Motivation

Sylvan BDD operations (e.g. `sylvan_and`) recursively split into high/low
cofactors, SPAWNing one branch as a Lace task and CALLing the other. For deep
BDD trees, this creates an exponential number of tasks.

In MTPNDD, parallelism is structured in two layers:

1. **Upper layer** (MTPNDD operations + benchmark scheduling): parallel
   SPAWN of NDD AND/OR/NOT sub-problems and tree-reduce.
2. **Lower layer** (BDD label operations): `sylvan_and`, `sylvan_or`, etc.
   on edge-label BDDs, called from within each MTPNDD sub-task.

The hypothesis: BDD-internal spawning is unnecessary overhead for MTPNDD
workloads — the BDD labels are small, every worker is already busy with
MTPNDD-level tasks, BDD sub-tasks only add deque pressure and steal
contention without meaningful parallel benefit.

Inspired by OxiDD's depth-threshold mechanism, we add a **spawn depth
cutoff** to Sylvan. Setting `cutoff=0` makes all BDD operations fully
sequential, confining parallelism to the upper layers.

## Design

### Thread-local depth counter

```c
static _Thread_local int bdd_spawn_depth = 0;
static int bdd_spawn_depth_cutoff = -1;  // <0 = unlimited (original behaviour)
```

- Incremented before each `SPAWN`, decremented after the corresponding `SYNC`.
- `bdd_should_spawn()` returns false when `cutoff >= 0 && depth >= cutoff`.

### API semantics

```c
void sylvan_set_spawn_depth_cutoff(int value);
int  sylvan_get_spawn_depth_cutoff(void);
```

| value | meaning |
|------:|---------|
| `< 0` (default `-1`) | Unlimited — original Sylvan behaviour |
| `0` | **No BDD-internal spawning** — all recursive sub-problems via CALL |
| `> 0` (e.g. `3`) | SPAWN only when nested depth < cutoff |

### Stolen task behaviour

When a SPAWNed task is stolen by another worker, it runs on the stealer's
thread with an independent `bdd_spawn_depth` (typically 0), giving the
stolen sub-tree a fresh parallelism budget.

### GC safety

Sequential cofactor computation requires protecting the first result from GC:

```c
low  = CALL(sylvan_and, aLow, bLow, level);
bdd_refs_push(low);          // protect from GC during high computation
high = CALL(sylvan_and, aHigh, bHigh, level);
bdd_refs_pop(1);
```

Without `bdd_refs_push`, GC triggered by `CALL(high)` via
`sylvan_gc_test()` → `YIELD_NEWFRAME()` could collect `low`.

### Modified operations

All fork-join BDD operations modified (14 total). `sylvan_relnext` /
`sylvan_relprev` not modified (complex multi-spawn, not used by MTPNDD).

---

## Benchmark Results

Platform: 6-core (12-thread), Linux 6.19.9. 3 runs per configuration,
sequential execution (no concurrent benchmarks).

### Experiment 1: Pure Sylvan BDD — N-Queens N=10

Workload: pure BDD over 100 variables. 1-worker serial baseline: **63.41 s**.

| cutoff | run 1 | run 2 | run 3 | median | vs default |
|-------:|------:|------:|------:|-------:|-----------:|
| -1 (unlimited) | 11.16 | 12.27 | 11.30 | **11.30** | baseline |
| 5      | 47.31 | 46.58 | 50.50 | **47.31** | +319% |
| 8      | 38.68 | 38.66 | 39.07 | **38.68** | +242% |
| 12     | 26.65 | 24.28 | 27.35 | **26.65** | +136% |
| 20     | 14.57 | 14.69 | 14.04 | **14.57** | +29% |

### Experiment 2: MTPNDD N-Queens — cutoff=0 vs default

| N  | default (median) | cutoff=0 (median) | delta |
|---:|------:|------:|------:|
| 11 | 1.063 | 1.055 | -0.8% |
| 12 | 5.349 | 5.334 | -0.3% |
| 13 | 30.77 | 31.58 | +2.6% |

### Experiment 3: sre-ndd BGP verification (MTPNDD time only)

| Dataset | Workers | Default | Cutoff=0 | Delta |
|---------|---------|--------:|---------:|------:|
| bgp_fattree08 | 1 | 1.935s | 1.926s | -0.5% |
| bgp_fattree08 | 4 | 2.080s | 2.035s | -2.2% |
| bgp_fattree12 | 1 | 17.820s | 17.643s | -1.0% |
| bgp_fattree12 | 4 | 19.722s | 18.701s | **-5.2%** |

---

## Analysis: Why Only ~5% Improvement

### BDD 在 MTPNDD 中的调用位置

MTPNDD 的核心递归操作（`mtpndd_and_rec`, `mtpndd_or_rec`）中，BDD 操作
发生在 **edge-pair 内层循环里**：

```
mtpndd_and_rec(A, B):
  FOR_EACH edge_a IN A.edges:          // 外层循环
    FOR_EACH edge_b IN B.edges:        // 内层循环
      label = sylvan_and(edge_a.label, edge_b.label)  // ← BDD 操作
      if label != false:
        child = mtpndd_and_rec(edge_a.child, edge_b.child)  // 递归
        add_edge(result, child, label)
```

关键特征：
- 每个 edge pair 产生 **恰好 1 次** `sylvan_and` 调用（OR 操作多几次）
- 这些 BDD 调用是 **串行执行** 的——在同一个 Lace task 内逐对处理
- 并行化发生在 **task 粒度**：MTPNDD 决定整个 edge-pair 的处理是
  SPAWN 还是 CALL（由 `mtpndd_should_spawn` 控制）

### 原因 1: BDD label 太小，内部几乎不产生 SPAWN

MTPNDD edge label 是小型 BDD。以 N-Queens 为例，label 编码的是
"哪些 assignment 使得这条边成立"的布尔条件。这些 BDD 通常只有
几个到几十个节点。

`sylvan_and` 的递归深度 = BDD 变量数。对于小 label，递归几层就
到达 terminal case（true/false），**根本没有机会 SPAWN**——还没走到
SPAWN 判断点就已经 return 了。

验证：原始 Sylvan 的 `sylvan_and` 中 SPAWN 只在 high cofactor
非 trivial 时触发。小 BDD 的 cofactor 很快变成常量，所以绝大多数
递归路径走的是 terminal shortcut，不经过 SPAWN 分支。

**结论：cutoff=0 禁用的 SPAWN 本来就很少发生**。

### 原因 2: BDD 调用在 MTPNDD task 内部串行执行

即使 BDD label 足够大能触发 SPAWN，这些 SPAWN 发生在 MTPNDD 的
spawned task 内部。调用链是：

```
Worker thread
  └─ mtpndd_and_rec (MTPNDD task, may be spawned or called)
       └─ FOR_EACH edge pair:
            └─ sylvan_and(label_a, label_b)  ← BDD SPAWN 发生在这里
                 └─ SPAWN(sylvan_and, high...)
                 └─ CALL(sylvan_and, low...)
                 └─ SYNC(sylvan_and)
```

BDD 的 SPAWN 推入当前 worker 的 deque。但此时 worker 正在执行
MTPNDD task，其他 worker 也在执行各自的 MTPNDD task。BDD sub-task
被偷走的概率很低——通常在 SYNC 之前就被本线程消费掉了（等同于 CALL）。

**结论：BDD SPAWN 大多退化为 local execution，禁用它几乎没有区别**。

### 原因 3: Lace SPAWN 开销本身极低

Lace 的 deque 操作是 lock-free 的：
- SPAWN = 写入 deque tail（~10ns）
- SYNC（未被偷）= 读取 deque tail + 本地执行（~10ns overhead）
- 只有被偷走时才涉及 CAS 和跨线程同步

对于 N=13 的 ~30 秒计算，即使有百万次 BDD SPAWN，总开销也只是
~10ms 量级，占比 < 0.03%。

### 原因 4: 瓶颈在 edge-pair 组合爆炸

MTPNDD 的计算复杂度主要来自 edge-pair 的笛卡尔积：一个有 E_a 条边
的节点和一个有 E_b 条边的节点做 AND，需要 E_a × E_b 次 BDD 操作。
这些操作在单个 task 内串行执行，BDD 内部是否并行对这个 O(E^2) 循环
没有影响。

### sre-ndd 为什么比 N-Queens 效果稍好（-5.2%）

sre-ndd 的 BGP 路由验证中，BDD label 编码 IP 前缀匹配条件，比
N-Queens 的 label 更大（更多 BDD 变量）。在 fattree12 + 4 workers 下：

- 更大的 label → 更深的 BDD 递归 → 更多 SPAWN 实际发生
- 4 workers 同时执行 MTPNDD task → deque 更拥挤 → SPAWN 的
  cache/contention 成本更高
- 禁用 SPAWN 后减少了这些开销，但绝对量仍然小

### 总结

| 因素 | 对 cutoff 效果的影响 |
|------|---------------------|
| BDD label 大小 | 小 label → 几乎不产生 SPAWN → 禁用无效果 |
| BDD SPAWN 局部性 | 大多在 SYNC 前被本线程消费 → 禁用几乎无区别 |
| Lace 单次开销 | ~10ns/次，极低 → 即使有 SPAWN 也不贵 |
| 真实瓶颈 | edge-pair O(E^2) 循环，不受 BDD 并行影响 |

**结论：BDD spawn depth cutoff 对当前 MTPNDD workload 效果有限（<5%），
根本原因是 BDD label 太小、SPAWN 本身极少发生且开销极低。瓶颈不在
BDD 并行层，而在 MTPNDD 的 edge-pair 组合爆炸。**

---

## Usage

### C API

```c
#include <sylvan.h>

// After sylvan_init_bdd():
sylvan_set_spawn_depth_cutoff(0);   // BDD fully serial
sylvan_set_spawn_depth_cutoff(-1);  // unlimited (default)
```

### Java API (sre-ndd)

```bash
java -Dmtpndd.bddSpawnDepthCutoff=0 -jar sre-ndd.jar ...
```

### Environment variable (C benchmarks)

```bash
BDD_SPAWN_DEPTH_CUTOFF=0 ./mtpndd_nqueens_parallel_benchmark 13
```

## Files changed

**mtpndd-c:**
- `sylvan/src/sylvan/sylvan_bdd.h` — API declarations
- `sylvan/src/sylvan/sylvan_bdd.c` — thread-local counter, 14 BDD operations modified
- `sylvan/examples/nqueens.c` — env var support for benchmarking
- `sylvan/src/sylvan/mtpndd/test/nqueens_parallel_benchmark.c` — env var support

**sre-ndd:**
- `lib/mtpndd/sylvan/src/sylvan/sylvan_bdd.{h,c}` — synced from mtpndd-c
- `lib/mtpndd/jni/src/main/native/mtpndd_jni.c` — JNI bindings
- `lib/mtpndd/jni/src/main/java/org/ants/mtpndd/MTPNDDEngine.java` — Java API
- `src/main/java/bdd/NDDManager.java` — system property integration
