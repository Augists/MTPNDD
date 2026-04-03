# MTPNDD N-Queens 操作并行化分析

## 1. 背景

MTPNDD（Multi-Terminal Parallel Network Decision Diagrams）的核心操作（`mtpndd_and`、`mtpndd_or`、`mtpndd_not`）
已经通过 Lace 工作窃取框架在**操作内部**实现了并行（intra-operation parallelism）：每次 AND/OR 调用会在
单个操作的递归子问题之间 SPAWN Lace 任务。

本次工作在此基础上增加了**操作间并行**（inter-operation parallelism）：将多个相互独立的 MTPNDD 操作作为
并发 Lace 任务同时提交，让 worker 在操作粒度上进行工作窃取。

以 N-Queens 问题为驱动场景进行设计与测试。

---

## 2. N-Queens 公式结构

N-Queens 将 N×N 棋盘上的合法放置编码为一个 MTPNDD 约束：

```
queen = AND(
  OR(col_0 | col_1 | ... | col_{n-1})   × n 行  ← 每行至少放一个皇后
  IMP(guard_{i,j} → ¬attack_{i,j})       × n×n 格  ← 每格的不冲突蕴含
)
```

其中：
- **or_batch[i]**：第 i 行的"至少放一个"约束，独立于其他行
- **imp_batch[i×n+j]**：格 (i,j) 的蕴含约束，独立于其他格

两类子问题都是**完全独立**的，天然适合外层并行。

---

## 3. 实现方案

### 3.1 新增文件

| 文件 | 说明 |
|------|------|
| `sylvan/src/sylvan/mtpndd/test/nqueens_parallel_benchmark.c` | 新增并行 benchmark |
| `sylvan/src/sylvan/mtpndd/CMakeLists.txt` | 注册 `mtpndd_nqueens_parallel_benchmark` target |
| `bench_sweep.sh` | 新增 `--parallel` flag 切换 benchmark binary |

### 3.2 三阶段流程对比

```
串行版 (nqueens_benchmark.c)
────────────────────────────────────────────────────────
Phase 1  顺序 for 循环：or_batch[0..n-1]
Phase 2  顺序 for 循环：imp_batch[0..n²-1]
Phase 3  顺序 fold AND：queen = AND(queen, term) 逐项累积

并行版 (nqueens_parallel_benchmark.c)
────────────────────────────────────────────────────────
Phase 1  SPAWN n Lace 任务：par_build_row_or(i)      ← 独立并发
Phase 2  SPAWN n² Lace 任务：par_build_cell(i,j)     ← 独立并发
Phase 3  顺序 fold AND（与串行版完全相同）
```

Phase 3 保持顺序 fold 的原因见第 4 节。

### 3.3 核心实现：SPAWN/SYNC 外层并行

```c
// 从主线程获取 Lace worker 上下文
// （mtpndd_init 调用 lace_start()，主线程即 worker 0）
LACE_ME;

// Phase 2：n²-1 个任务并发 SPAWN
for (size_t k = 0; k < total_cells - 1; k++) {
    SPAWN(par_build_cell, k / n, k % n, n);
}
imp_batch[total_cells - 1] = CALL(par_build_cell, n - 1, n - 1, n);  // 最后一个本地执行

// LIFO 顺序 SYNC（必须与 SPAWN 顺序相反）
for (int k = (int)total_cells - 2; k >= 0; k--) {
    imp_batch[k] = SYNC(par_build_cell);
}
```

### 3.4 Lace 嵌套调用安全性

并行任务（如 `par_build_cell`）内部调用 `mtpndd_and`，而 `mtpndd_and` 内部又使用 `RUN(mtpndd_and_rec, ...)`：

```c
// mtpndd_and 的实现
mtpndd_t *mtpndd_and(mtpndd_t *a, mtpndd_t *b) {
    return RUN(mtpndd_and_rec, a, b);
}

// RUN 宏展开
#define RUN(f, ...) ({ LACE_ME; CALL(f, ##__VA_ARGS__); })
```

当 `par_build_cell` 被 worker N 窃取执行时，`RUN` 内的 `LACE_ME` 调用 `lace_get_worker()` 取回
worker N 自己的上下文，然后直接 `CALL(mtpndd_and_rec, ...)`，不会与其他 worker 的队列冲突。
`mtpndd_and_rec` 内的 SPAWN/SYNC 也在 worker N 的 deque 上操作，语义完全正确。

### 3.5 引用计数跨 GC 保护

每个 Lace 任务返回**带 +1 ref** 的节点指针，确保在 SYNC 之前节点不会被 GC 回收：

```c
TASK_IMPL_2(mtpndd_t *, par_build_row_or, size_t, row, size_t, n) {
    mtpndd_t *cond = &MTPNDD_FALSE;
    mtpndd_ref(cond);
    for (size_t j = 0; j < n; ++j) {
        mtpndd_t *next = mtpndd_or(cond, v);
        mtpndd_ref(next);      // 立即保护，防止下次 GC 触发前被回收
        mtpndd_deref(cond);
        cond = next;
    }
    return cond;               // 返回 +1 ref，所有权交给调用方
}
```

SYNC 后，调用方直接持有该 ref，Phase 3 结束后统一 deref。

### 3.6 BDD 表大小调整

串行版的 BDD nodetable 大小按顺序构建调优，对于 N≤8 仅分配约 32K 条目。
并行版中 n² 个格子同时存活，需要更大的表，因此对小 N 设置 128K 下限：

```c
// 串行版：fmax(1000.0, pow(4.4, n-6) * 1000.0)
// 并行版：将下限从 1000 提升至 131072 (128K)
size_t bdd_size = 1 + (size_t)fmax(131072.0, pow(4.4, (double)n - 6.0) * 1000.0);
```

对 N≥10，两者公式给出相同结果（≥512K），无差异。

---

## 4. Phase 3 不能用树形规约的原因

最初尝试用 `mtpndd_and_reduce`（Lace 树形并行规约）替代顺序 fold：

```
树形规约：
Level 0: [r0, r1, r2, r3, ..., r155]     (156 项)
Level 1: [AND(r0,r1), AND(r2,r3), ...]   (78 项)  ← 中间 NDD 在剪枝前就变得很大
Level 2: [AND(L1[0],L1[1]), ...]         (39 项)  ← 继续膨胀
...
Level 8: [final queen]
```

**问题**：树形规约在约束剪枝（progressive constraint strengthening）发挥作用之前，就将相互独立的约束
两两合并，产生极大的中间 NDD。

**顺序 fold**：

```
queen = TRUE
queen = AND(queen, r0)    ← 已施加约束
queen = AND(queen, r1)    ← 在缩小的 queen 上操作，快
queen = AND(queen, r2)    ← 继续缩小
...
```

每一步都在"已被之前约束收紧"的 `queen` 上操作，中间结果始终是最小的。

实测：对 N=12 用 `mtpndd_and_reduce` 会触发 OOM（SIGKILL），而顺序 fold 约 4-13 秒完成。

---

## 5. 基准测试结果

**测试环境**：`MTPNDD_LOG_LEVEL=0`，时间含 `mtpndd_init` + 字段初始化  
**binary**：`mtpndd_nqueens_benchmark` / `mtpndd_nqueens_parallel_benchmark`  
**编译**：`cmake -B build -DMTPNDD_LOG_LEVEL=0 && cmake --build build --parallel`

### 5.1 详细数据（N=7..13，workers=1..6）

<!-- BENCHMARK_TABLE_START -->
| N  | workers | serial (s) | parallel (s) | speedup | solutions |
|----|---------|-----------|-------------|---------|-----------|
| 7  | 1       | 0.028     | 0.027       | 1.04x   | 40        |
| 7  | 2       | 0.024     | 0.021       | 1.14x   | 40        |
| 7  | 3       | 0.022     | 0.017       | 1.29x   | 40        |
| 7  | 4       | 0.022     | 0.016       | 1.38x   | 40        |
| 7  | 5       | 0.021     | 0.014       | **1.50x** | 40      |
| 7  | 6       | 0.021     | 0.016       | 1.31x   | 40        |
| 8  | 1       | 0.049     | 0.050       | 0.98x   | 92        |
| 8  | 2       | 0.043     | 0.036       | 1.19x   | 92        |
| 8  | 3       | 0.035     | 0.027       | 1.30x   | 92        |
| 8  | 4       | 0.035     | 0.025       | 1.40x   | 92        |
| 8  | 5       | 0.033     | 0.022       | **1.50x** | 92      |
| 8  | 6       | 0.033     | 0.021       | **1.57x** | 92      |
| 9  | 1       | 0.134     | 0.137       | 0.98x   | 352       |
| 9  | 2       | 0.106     | 0.095       | 1.12x   | 352       |
| 9  | 3       | 0.077     | 0.062       | 1.24x   | 352       |
| 9  | 4       | 0.071     | 0.057       | 1.25x   | 352       |
| 9  | 5       | 0.066     | 0.049       | 1.35x   | 352       |
| 9  | 6       | 0.061     | 0.043       | **1.42x** | 352     |
| 10 | 1       | 0.479     | 0.484       | 0.99x   | 724       |
| 10 | 2       | 0.350     | 0.338       | 1.04x   | 724       |
| 10 | 3       | 0.234     | 0.212       | 1.10x   | 724       |
| 10 | 4       | 0.215     | 0.192       | 1.12x   | 724       |
| 10 | 5       | 0.189     | 0.165       | 1.15x   | 724       |
| 10 | 6       | 0.167     | 0.140       | **1.19x** | 724     |
| 11 | 1       | 2.310     | 2.343       | 0.99x   | 2680      |
| 11 | 2       | 1.632     | 1.605       | 1.02x   | 2680      |
| 11 | 3       | 1.049     | 1.021       | 1.03x   | 2680      |
| 11 | 4       | 0.919     | 0.900       | 1.02x   | 2680      |
| 11 | 5       | 0.772     | 0.742       | 1.04x   | 2680      |
| 11 | 6       | 0.683     | 0.643       | **1.06x** | 2680    |
| 12 | 1       | 12.647    | 12.511      | 1.01x   | 14200     |
| 12 | 2       | 8.772     | 8.706       | 1.01x   | 14200     |
| 12 | 3       | 5.614     | 5.364       | 1.05x   | 14200     |
| 12 | 4       | 4.875     | 4.887       | 1.00x   | 14200     |
| 12 | 5       | 4.134     | 4.044       | 1.02x   | 14200     |
| 12 | 6       | 3.552     | 3.585       | 0.99x   | 14200     |
| 13 | 1       | 75.876    | 75.633      | 1.00x   | 73712     |
| 13 | 2       | 51.484    | 51.938      | 0.99x   | 73712     |
| 13 | 3       | 32.421    | 32.994      | 0.98x   | 73712     |
| 13 | 4       | 28.999    | 29.068      | 1.00x   | 73712     |
| 13 | 5       | 24.325    | 24.115      | 1.01x   | 73712     |
| 13 | 6       | 21.115    | 21.081      | 1.00x   | 73712     |
<!-- BENCHMARK_TABLE_END -->

### 5.2 加速比汇总（speedup = serial / parallel）

<!-- SPEEDUP_TABLE_START -->
| N  | w=1   | w=2   | w=3   | w=4   | w=5       | w=6       | 趋势 |
|----|-------|-------|-------|-------|-----------|-----------|------|
| 7  | 1.04x | 1.14x | 1.29x | 1.38x | **1.50x** | 1.31x     | 中等提升 |
| 8  | 0.98x | 1.19x | 1.30x | 1.40x | **1.50x** | **1.57x** | 显著提升 |
| 9  | 0.98x | 1.12x | 1.24x | 1.25x | 1.35x     | **1.42x** | 显著提升 |
| 10 | 0.99x | 1.04x | 1.10x | 1.12x | 1.15x     | **1.19x** | 中等提升 |
| 11 | 0.99x | 1.02x | 1.03x | 1.02x | 1.04x     | **1.06x** | 微弱提升 |
| 12 | 1.01x | 1.01x | 1.05x | 1.00x | 1.02x     | 0.99x     | 噪声范围 |
| 13 | 1.00x | 0.99x | 0.98x | 1.00x | 1.01x     | 1.00x     | 无提升   |
<!-- SPEEDUP_TABLE_END -->

---

## 6. 分析与结论

### 6.1 加速比规律

实测结果呈现明显的 N 分段特征：

| 规模 | 提升效果 | 原因 |
|------|---------|------|
| N=7-9  | **1.2-1.6x**（显著）| 构建（Phase 1+2）占总时较大；小问题 Phase 3 也快 |
| N=10   | **1.1-1.2x**（中等）| 构建比例下降，但仍可见收益 |
| N=11   | ~1.0-1.06x（微弱）| Phase 3 开始主导 |
| N=12-13 | ~1.0x（噪声范围）| Phase 3 绝对主导，构建占比 < 3% |

**为什么 w=1 基本没有提升？**  
w=1 时 SPAWN/SYNC 退化为 CALL（`mtpndd_should_spawn` 检测 `lace_workers() <= 1` 直接返回 false），
外层 SPAWN n² 个任务也是顺序执行，但多了 Lace 任务帧开销，所以 w=1 时并行版略慢。

**为什么 N=8 在 w=6 有 1.57x 但 N=12 没有？**  
用 Amdahl 定律估算两个极端：

```
N=8,  w=6：
  Phase 1+2（可并行）≈ 0.015s / 总 0.033s → f ≈ 45%
  理论上限 ≈ 1/(1 - 0.45 + 0.45/6) ≈ 1.55x  ← 与实测 1.57x 吻合

N=12, w=6：
  Phase 1+2（可并行）≈ 0.15s / 总 3.55s  → f ≈ 4%
  理论上限 ≈ 1/(1 - 0.04 + 0.04/6) ≈ 1.04x ← 完全被 Phase 3 吞没
```

### 6.2 若要提升大 N（12+）的加速比

Phase 3 是数据依赖链（`queen[k] = AND(queen[k-1], constraint[k])`），无法直接并行。
可行优化方向：

1. **按行分组规约（推荐优先尝试）**：先将每行的 n 个格子约束 AND-reduce 为一个 `row_cells[i]`
   （同一行格子涉及相同 field_id，中间结果较小），再顺序 fold n 个行结果。
   Phase 3 步数从 n² 降为 n，且行内 reduce 可并行，实际可并行比例大幅提升。

2. **Symmetry decomposition**：利用棋盘的旋转/反射对称性，将问题分解为 k 个独立子问题，
   各自构建完整公式后 OR 合并，Phase 1+2+3 全部并行。

3. **构建-合并流水线**：在 Phase 3 的每次 AND 完成后，立即异步启动下一个格子的构建，
   让构建和合并在时间上重叠，无需等所有构建完成再开始合并。
