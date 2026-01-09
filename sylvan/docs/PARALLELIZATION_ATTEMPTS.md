# MTPNDD 并行化尝试记录

## 目标
实现MTPNDD的fork-join并行化，期望：
- **Worker=1时**: 性能≈串行版本（~11-12s）
- **Worker>1时**: 有显著并行加速（目标：接近Sylvan BDD的~3s性能）

---

## 尝试1：全面TASK改造 + SPAWN/SYNC（❌ 失败，已回退）

### 实施时间
2026-01-09

### 实施方案

#### 1. 将`mtpndd_and_rec`改造为TASK
```c
// 前：普通函数，返回error code
static mtpndd_error_t mtpndd_and_rec(mtpndd_t *a, mtpndd_t *b, mtpndd_t **result);

// 后：LACE TASK，直接返回结果
TASK_IMPL_2(mtpndd_t*, mtpndd_and_rec, mtpndd_t*, a, mtpndd_t*, b);
```

#### 2. 修改辅助函数签名传递worker/task参数
```c
// 前
static mtpndd_error_t mtpndd_and_same_field(
    edge_bucket_entry_t *entry_a, edge_bucket_entry_t *entry_b,
    mtpndd_edge_t *res_edges, mtpndd_temp_ref_list_t *temp_refs);

// 后
static mtpndd_error_t mtpndd_and_same_field(
    WorkerP *__lace_worker, Task *__lace_dq_head,  // 新增
    edge_bucket_entry_t *entry_a, edge_bucket_entry_t *entry_b,
    mtpndd_edge_t *res_edges, mtpndd_temp_ref_list_t *temp_refs);
```

#### 3. 在递归点添加SPAWN/SYNC
```c
// mtpndd_and_same_field 中：
SPAWN(mtpndd_and_rec, entry_a->child, entry_b->child);
mtpndd_node_t *sub_result = SYNC(mtpndd_and_rec);

// mtpndd_and_diff_field 中：
SPAWN(mtpndd_and_rec, entry_a->child, b);
mtpndd_node_t *sub_result = SYNC(mtpndd_and_rec);
```

#### 4. 添加粒度控制
```c
// 只在子节点足够复杂时SPAWN
if (entry_a->child->edges && entry_a->child->edges->edge_count > 10 &&
    entry_b->child->edges && entry_b->child->edges->edge_count > 10) {
    SPAWN(...);
    sub_result = SYNC(...);
} else {
    sub_result = CALL(...);  // 小问题直接调用
}
```

### 实验结果

#### 性能测试（NQueens N=12）

| Worker数 | 时间(s) | vs Worker=1 | vs改造前(11.8s) |
|---------|---------|-------------|----------------|
| **1**   | **16.681** | **基准**    | **+41% 慢** ❌ |
| 2       | 16.360  | +2% 快      | +39% 慢 ❌ |
| 6       | 16.615  | 持平        | +41% 慢 ❌ |

#### 对比Sylvan BDD
- Sylvan BDD (N=12, 6 workers): ~3s
- MTPNDD (N=12, 6 workers): 16.6s
- **差距**: 5.5x慢

### 问题分析

#### 问题1：TASK改造带来巨大开销（+41%）
**根本原因**：将核心递归函数改为TASK后，**每次调用**都经过LACE任务调度系统，即使不SPAWN也有开销：

1. **任务创建开销**：
   - 每次`mtpndd_and_rec`调用都需要通过LACE的WRAP/CALL宏
   - 涉及task frame的设置和清理

2. **调用约定变化**：
   - 原来：直接函数调用（几个CPU周期）
   - TASK后：需要传递worker和task参数，增加栈操作

3. **影响范围广**：
   - `mtpndd_and_rec`是最频繁调用的函数（每个NDD节点对的AND操作）
   - NQueens N=12大约调用数百万次
   - 累积开销巨大：11.8s → 16.7s (+5s)

#### 问题2：并行加速微弱（Worker=2仅+2%）
**根本原因**：SPAWN粒度控制策略不佳

1. **当前策略的问题**：
   ```c
   // 在每个edge pair的递归点SPAWN
   if (edge_count > 10) {
       SPAWN(mtpndd_and_rec, child_a, child_b);
       result = SYNC(mtpndd_and_rec);
   }
   ```
   - **问题**：粒度仍然太细，同一个AND操作内的不同edge pairs顺序执行
   - **结果**：多个worker大部分时间在等待，无法并行处理同一AND的多个edge pairs

2. **Sylvan的优势**：
   - BDD是二叉结构：high分支SPAWN，low分支CALL
   - 两个分支可以真正并行
   - NDD是多路分支（多个edges），需要不同策略

3. **NDD特有挑战**：
   - same_field情况：O(|edges_a| × |edges_b|) 个edge pairs
   - 当前策略：逐个pair处理，每个pair可能SPAWN子递归
   - **需要**：同时处理多个pairs，批量SPAWN/SYNC

#### 问题3：与Sylvan差距大（5.5x）
除了并行化问题，还有结构差异：
- BDD：二叉，决策层级清晰
- NDD：多路分支，每个节点有多条edges，结构更复杂
- 即使完美并行化，NDD也可能比BDD慢2-3x（结构本身的复杂度）

### 为什么失败：设计思路错误

#### 错误的假设
**"将递归函数改为TASK，然后在递归点SPAWN就能并行"**

这个思路忽略了：
1. TASK不是零成本抽象，频繁调用的函数不应该是TASK
2. 并行的粒度应该更粗：不是"每次递归都考虑SPAWN"，而是"在合适的层次批量并行"

#### Sylvan的真正做法
查看`sylvan_bdd.c`：
```c
TASK_IMPL_3(BDD, sylvan_and, BDD, a, BDD, b, BDDVAR, prev_level) {
    // Terminal cases...

    // 递归计算：SPAWN一个分支，CALL另一个
    if (aHigh != trivial && bHigh != trivial) {
        bdd_refs_spawn(SPAWN(sylvan_and, aHigh, bHigh, level));
        n=1;
    }
    low = CALL(sylvan_and, aLow, bLow, level);
    if (n) {
        high = bdd_refs_sync(SYNC(sylvan_and));
    }

    result = sylvan_makenode(level, low, high);
    return result;
}
```

**关键点**：
- `sylvan_and`是TASK，但这是因为它**本身就是可并行的最小单元**
- BDD只有两个分支，天然适合fork-join
- 每次调用本身就可能被SPAWN到其他worker

---

## 正确的并行化思路

### 方案A：保持核心函数为普通函数，创建并行包装器（推荐）

#### 设计思路
```c
// 核心递归：保持为普通函数（无TASK开销）
static mtpndd_t *mtpndd_and_rec(mtpndd_t *a, mtpndd_t *b);

// 并行包装器：仅在需要并行时使用
TASK_IMPL_2(mtpndd_t*, mtpndd_and_parallel, mtpndd_t*, a, mtpndd_t*, b) {
    // 判断是否值得并行
    if (should_parallelize(a, b)) {
        // 并行处理edges（批量SPAWN/SYNC）
        return mtpndd_and_parallel_impl(a, b);
    } else {
        // 直接调用串行版本
        return mtpndd_and_rec(a, b);
    }
}
```

#### 优势
1. ✅ Worker=1性能：直接调用普通函数，无TASK开销
2. ✅ Worker>1性能：在高层次并行，粒度合适
3. ✅ 灵活性：可以精确控制何时并行

#### 实施重点
1. **并行粒度**：在`mtpndd_and_rec`的最外层判断
   - 如果两个NDD都足够大（比如field_id差距大，edges数量多）
   - 才使用并行版本

2. **批量SPAWN/SYNC**：
   ```c
   // 对于same_field的多个edge pairs：
   for (int i = 0; i < n_pairs; i++) {
       SPAWN(mtpndd_and_parallel, edge_pairs[i].a, edge_pairs[i].b);
   }
   for (int i = 0; i < n_pairs; i++) {
       results[i] = SYNC(mtpndd_and_parallel);
   }
   ```

3. **Granularity control**：
   - 参考Sylvan的level-based控制
   - 只在field_id变化超过阈值时才考虑并行

### 方案B：选择性TASK化关键路径

类似Sylvan，但针对NDD结构调整：
- 将`mtpndd_and_rec`改为TASK
- **但**：添加一个非TASK的fast path版本`mtpndd_and_rec_inline`
- 小问题调用inline版本，大问题使用TASK版本

### 方案C：基于work-stealing的edge-level并行

更激进的方案：
- 在edge迭代层面并行（而非递归层面）
- 使用LACE的work queue并行处理edge pairs
- 需要重构代码结构

---

## 尝试2：方案A - 并行包装器（⚠️ 基本无效）

### 实施时间
2026-01-09（尝试1失败后）

### 实施方案

#### 设计思路
- **保持`mtpndd_and_rec`为普通函数**（无TASK开销）
- 创建TASK并行包装器`mtpndd_and_parallel_rec`
- 在入口函数`mtpndd_and`中根据条件选择串行/并行

#### 代码实现
```c
// 1. TASK声明
TASK_DECL_2(mtpndd_t*, mtpndd_and_parallel_rec, mtpndd_t*, mtpndd_t*);

// 2. TASK实现（简单包装）
TASK_IMPL_2(mtpndd_t*, mtpndd_and_parallel_rec, mtpndd_t*, a, mtpndd_t*, b) {
    mtpndd_t *result = NULL;
    if (mtpndd_and_rec(a, b, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

// 3. 并行判断逻辑
static inline bool mtpndd_should_parallelize(mtpndd_t *a, mtpndd_t *b) {
    if (lace_workers() <= 1) return false;
    if (mtpndd_is_terminal(a) || mtpndd_is_terminal(b)) return false;

    // Conservative heuristic
    if (a->field_id >= 2 && b->field_id >= 2) {
        size_t edges_a = a->edges ? a->edges->edge_count : 0;
        size_t edges_b = b->edges ? b->edges->edge_count : 0;
        if (edges_a >= 3 && edges_b >= 3) {
            return true;
        }
    }
    return false;
}

// 4. 入口函数（选择性并行）
mtpndd_t *mtpndd_and(mtpndd_t *a, mtpndd_t *b) {
    if (mtpndd_should_parallelize(a, b)) {
        return RUN(mtpndd_and_parallel_rec, a, b);
    }
    // Serial path (no TASK overhead)
    mtpndd_t *result = NULL;
    if (mtpndd_and_rec(a, b, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}
```

### 实验结果

#### 性能测试（NQueens N=12）

| Worker数 | 时间(s) | vs Worker=1 | 说明 |
|---------|---------|-------------|------|
| **1**   | **15.962** | **基准**    | ✅ 无退化 |
| 2       | 16.096  | -0.8%       | ⚠️ 轻微退化 |
| 6       | 15.802  | +1.0%       | ⚠️ 几乎无加速 |

**对比尝试1**：
- 尝试1 Worker=1: 16.7s
- 尝试2 Worker=1: 16.0s
- **改进**: 保持了串行性能（因为Worker=1走串行路径）✓

### 问题分析

#### 问题：并行加速几乎为零

**根本原因**：包装器只是在顶层判断，内部递归仍然是串行的

1. **顶层并行不够**：
   ```
   用户调用: mtpndd_and(A, B)
      ↓
   判断should_parallelize -> true (field_id大，edges多)
      ↓
   RUN(mtpndd_and_parallel_rec, A, B)  // TASK化
      ↓
   mtpndd_and_rec(A, B)  // 但内部递归全是串行！
      ├─ FOR_EACH edge_a:
      │     FOR_EACH edge_b:
      │        mtpndd_and_rec(child_a, child_b)  // ← 串行调用
      ```

2. **真正并行的部分太少**：
   - 只有**最顶层的一个AND操作**被TASK化
   - 所有子递归调用仍然是普通函数调用（串行）
   - 多个workers只能在最外层"等待"，无法参与内部计算

3. **为什么Worker=6也没加速**：
   - 虽然顶层AND被RUN了，但LACE调度器发现：
     - 这个TASK内部全是串行代码
     - 没有任何SPAWN来创建子任务
     - 其他workers无事可做

#### 对比Sylvan的正确做法

Sylvan的`sylvan_and`：
```c
TASK_IMPL_3(BDD, sylvan_and, ...) {
    // ... terminal cases ...

    // ✅ 关键：在TASK内部SPAWN子任务
    bdd_refs_spawn(SPAWN(sylvan_and, aHigh, bHigh, level));
    low = CALL(sylvan_and, aLow, bLow, level);
    high = bdd_refs_sync(SYNC(sylvan_and));

    // 两个子分支可以并行
}
```

**关键差异**：
- Sylvan：TASK内部会SPAWN更多子任务（递归并行）
- 我们：TASK内部没有SPAWN（只有顶层是TASK，内部全串行）

### 为什么方案A失败

#### 错误假设
**"把入口包装成TASK就能并行"**

实际上：
- ✅ 顶层操作可以被多个workers执行（如果有多个AND调用）
- ❌ 但单个AND内部仍然是串行的（没有SPAWN子任务）
- ❌ NQueens测试中，大部分时间在一个大的AND操作内部

#### 真正需要的
**递归内部也要并行**：
- 不仅仅是`mtpndd_and`的入口是TASK
- `mtpndd_and_rec`内部的edge pairs处理也要并行
- 需要在递归调用点添加SPAWN/SYNC（但这回到了尝试1的问题）

### 结论

方案A失败的根本原因：
1. ✅ 避免了Worker=1的性能退化
2. ❌ 但没有实现真正的内部并行
3. ❌ 只有顶层TASK化是不够的
4. ❌ 需要在递归内部也有并行机制

### 用户关键Insight

在方案A测试后，用户指出核心问题：

> **"我们内部的边运算也应该时SPAWN并行的啊"**

这揭示了方案A失败的根本原因：
- ❌ 只在顶层`mtpndd_and`入口进行TASK包装
- ❌ 内部的edge pair递归调用（line 488, 513）仍然是串行的
- ✅ **需要在edge递归点也添加SPAWN**，就像Sylvan在`sylvan_and`内部SPAWN子分支一样

---

## 下一步行动计划

### 方案B：内部递归点也要SPAWN（当前方向）

#### 核心思路
```c
// 1. mtpndd_and_rec保持为TASK（可被SPAWN）
TASK_IMPL_2(mtpndd_t*, mtpndd_and_rec, mtpndd_t*, a, mtpndd_t*, b);

// 2. 在内部edge递归点添加SPAWN/SYNC
static mtpndd_error_t mtpndd_and_same_field(...) {
    FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(b->edges, entry_b) {
            // ✅ 关键：在这里SPAWN子递归
            if (should_spawn(entry_a->child, entry_b->child)) {
                SPAWN(mtpndd_and_rec, entry_a->child, entry_b->child);
                sub_result = SYNC(mtpndd_and_rec);
            } else {
                sub_result = CALL(mtpndd_and_rec, entry_a->child, entry_b->child);
            }
        }
    }
}
```

#### 关键设计要点

1. **避免Worker=1退化**：
   - 添加非常激进的granularity control
   - Worker=1时：never enter TASK path（使用inline fast path）
   - 或者：Worker=1时，should_spawn始终返回false，全用CALL

2. **SPAWN粒度控制**：
   - 只在子问题足够大时SPAWN（例如：edges > 5, field_id > 3）
   - 避免过度SPAWN导致调度开销

3. **批量SPAWN/SYNC**（可选优化）：
   - 收集所有edge pairs先SPAWN
   - 统一SYNC所有结果
   - 更好的并行度

#### 实施步骤

1. **阶段1：基础TASK化 + 内部SPAWN**
   - 将`mtpndd_and_rec`转为TASK
   - 在line 488和line 513添加条件SPAWN
   - 测试Worker=1性能（目标：无退化，≈16s）

2. **阶段2：优化granularity control**
   - 根据Worker=1结果调整should_spawn条件
   - 确保Worker=1走CALL路径
   - 测试Worker=2,6性能（目标：Worker=6有2x+加速）

3. **阶段3：批量SPAWN优化**（如果需要）
   - 如果单个SPAWN效果不好，尝试批量并行edge pairs
   - 测量并对比性能

#### 性能目标

| Worker | 目标时间 | 说明 |
|--------|---------|------|
| 1      | ≤16s    | 不能退化 |
| 2      | ~10-12s | 约25-30%加速 |
| 6      | ~6-8s   | 约2x加速 |

（Sylvan BDD的~3s是更高目标，考虑到NDD结构更复杂，初期目标是达到合理的并行加速）

---

## 尝试3：方案B - 内部递归点SPAWN（部分成功，需优化）

### 实施时间
2026-01-09（方案A测试后）

### 实施方案

#### 核心修改
根据用户insight，将SPAWN添加到内部edge递归点：

1. **mtpndd_and_rec改为TASK**：
   ```c
   TASK_IMPL_2(mtpndd_t*, mtpndd_and_rec, mtpndd_t*, a, mtpndd_t*, b);
   ```

2. **在edge递归点条件性SPAWN**：
   ```c
   // mtpndd_and_same_field (line 513-517):
   if (mtpndd_should_spawn(entry_a->child, entry_b->child)) {
       SPAWN(mtpndd_and_rec, entry_a->child, entry_b->child);
       sub_result = SYNC(mtpndd_and_rec);
   } else {
       sub_result = CALL(mtpndd_and_rec, entry_a->child, entry_b->child);
   }

   // mtpndd_and_diff_field (line 548-552): 类似实现
   ```

3. **Granularity control实现**：
   ```c
   static inline bool mtpndd_should_spawn(mtpndd_t *a, mtpndd_t *b) {
       if (lace_workers() <= 1) return false;
       if (mtpndd_is_terminal(a) || mtpndd_is_terminal(b)) return false;

       // Conservative heuristic
       if (a->field_id >= 2 && b->field_id >= 2) {
           size_t edges_a = a->edges ? a->edges->edge_count : 0;
           size_t edges_b = b->edges ? b->edges->edge_count : 0;
           if (edges_a >= 4 && edges_b >= 4) {
               return true;
           }
       }
       return false;
   }
   ```

### 实验结果

#### 性能测试（NQueens N=12）

| Worker数 | 时间(s) | vs Worker=1 | vs基准(11.8s) | 说明 |
|---------|---------|-------------|--------------|------|
| **1**   | **12.074** | **基准**    | +2.3%        | ✅ 接近基准 |
| 2       | 12.095  | +0.2%       | +2.5%        | ⚠️ 无加速 |
| 6       | 13.568  | **+12.4%**  | +15.0%       | ❌ 反而变慢 |

#### 对比Sylvan BDD
- Sylvan BDD (N=12, 6 workers): ~3s
- MTPNDD方案B (N=12, 6 workers): 13.6s
- **差距**: 4.5x慢

### 问题分析

#### 成功的部分 ✅
**Worker=1性能达标**：
- 12.074s vs 11.8s基准（+2.3%）
- Granularity control起作用：`lace_workers() <= 1`防止了TASK开销
- 证明了TASK化改造本身不会引入明显开销（当不SPAWN时）

#### 失败的部分 ❌
**Worker>1无并行加速，反而退化**：

1. **Granularity control太保守**：
   - 当前条件：`field_id >= 2 && edges >= 4`
   - 结果：几乎不触发SPAWN，workers闲置
   - Worker=2和Worker=6性能几乎一样，说明没有真正并行

2. **SPAWN点太少**：
   - 虽然在内部edge递归点添加了SPAWN
   - 但条件太严格，大部分子问题走CALL路径（串行）
   - 多个workers无法参与计算

3. **Worker=6反而慢12%的原因**：
   - LACE调度器开销：6个workers即使空闲也有开销
   - 极少的SPAWN不足以抵消调度成本
   - 可能存在cache line contention（多核间cache一致性开销）

### 为什么方案B初版失败

#### 正确的方向
- ✅ 识别出需要在内部递归点SPAWN
- ✅ TASK化改造正确（Worker=1无明显退化）
- ✅ 添加了granularity control框架

#### 错误的调参
- ❌ Granularity control太保守，不敢SPAWN
- ❌ 假设：担心过度SPAWN导致退化
- ❌ 实际：根本没有并行，多workers纯属浪费

---

## 下一步优化方向

### 优化1：放宽granularity control（优先）

**当前策略**：
```c
if (field_id >= 2 && edges >= 4) → SPAWN
```

**问题**：太保守，NQueens N=12的大部分节点不满足条件

**新策略**（激进）：
```c
// 方案1：更激进的阈值
if (field_id >= 1 && edges >= 2) → SPAWN

// 方案2：基于深度和workers
if (field_id >= 1 && lace_workers() > 1) → SPAWN

// 方案3：完全去掉field_id限制
if (edges >= 3 || field_id >= 2) → SPAWN
```

**目标**：
- Worker=1: 保持≤13s（允许1s退化）
- Worker=6: 达到8-10s（约30-40%加速）

### 优化2：批量SPAWN edge pairs

当前实现：
```c
FOR_EACH edge_pair:
    if (should_spawn) SPAWN → SYNC  // 逐个SPAWN
```

优化方案：
```c
// 收集所有需要SPAWN的pairs
FOR_EACH edge_pair:
    if (should_spawn) SPAWN(...)  // 全部SPAWN
FOR_EACH edge_pair:
    SYNC(...)  // 统一SYNC
```

**优势**：
- 更好的并行度：多个子任务同时执行
- 减少等待时间

### 优化3：动态granularity based on depth

参考Sylvan的level-based控制：
```c
static size_t g_spawn_depth_threshold = 0;  // 初始值，动态调整

bool should_spawn(mtpndd_t *a, mtpndd_t *b) {
    if (lace_workers() <= 1) return false;

    // 只在足够高的层级SPAWN（避免底层过度SPAWN）
    if (a->field_id < g_spawn_depth_threshold) {
        return true;  // 高层级：激进SPAWN
    }

    // 底层级：保守SPAWN
    return edges_a >= 4 && edges_b >= 4;
}
```

### Granularity优化尝试

尝试了多种granularity control策略：

| 策略 | 条件 | Worker=1 | Worker=2 | Worker=6 | 说明 |
|------|------|----------|----------|----------|------|
| **保守** | `field_id >= 2 && edges >= 4` | 12.074s | 12.095s (+0.2%) | 13.568s (+12.4%) | 几乎不SPAWN |
| **中等** | `field_id >= 1 && edges >= 2` | 11.974s | 12.513s (+4.5%) | 13.139s (+9.7%) | SPAWN增加，仍不够 |
| **激进** | `总是SPAWN非terminal` | 11.971s | 13.870s (+15.9%) | 15.230s (+27.2%) | 过度SPAWN |
| **平衡** | `field_id <= 3 或 edges >= 3` | 11.934s | 12.192s (+2.2%) | 13.290s (+11.4%) | 基于层级 |

### 结论

**方案B在当前实现下无法获得并行加速：**
- ✅ Worker=1性能达标（11.9-12.1s，接近11.8s基准）
- ❌ Worker>1全部退化（+2%到+27%）
- ⚠️ 所有granularity策略都失败

**根本问题**：
1. **SPAWN/SYNC开销巨大**：LACE的任务调度开销超过并行收益
2. **SPAWN位置可能不对**：在edge内部递归点SPAWN，而不是在edge pair循环层面
3. **NDD结构特性**：多路分支可能不适合这种fork-join模式
4. **Cache contention**：多核间共享数据结构（nodetable, cache）可能产生冲突

---

## 经验教训

### 1. 不要盲目套用模式
- Sylvan的TASK模式适合BDD（二叉，简单）
- NDD需要针对多路分支特点设计并行策略

### 2. 性能优先级
- **首要**：不能让Worker=1变慢
- **其次**：并行加速
- 如果为了并行牺牲串行性能，不如不并行

### 3. 粒度是关键
- TASK不是零成本
- 并行粒度要足够粗，才能抵消调度开销
- 测量和迭代：先保守（大粒度），再激进（小粒度）

### 4. 批量操作
- NDD的edge pairs天然适合批量并行
- fork-join模式要适配到批量SPAWN/SYNC

---

## 参考资料

### Sylvan源码
- `src/sylvan/sylvan_bdd.c`: BDD operations的TASK实现
- `src/sylvan/lace.h`: LACE API定义

### 相关文档
- `docs/PARALLEL_PERFORMANCE_ISSUE.md`: 之前发现的性能问题
- `docs/PERFORMANCE_ANALYSIS.md`: 优化历史和基准性能

### 测试数据
- NQueens N=12: 14200 solutions
- 串行基准: 11.8s (Worker=1, 32x cache)
- Sylvan BDD基准: ~3s (Worker=6)
