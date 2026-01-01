# MTPNDD 性能分析报告：C实现 vs Java版本

## 4. 具体改进建议

### 4.1 高优先级 [预期提升: 5-10x]

#### 4.1.1 修复操作缓存
**问题**: 操作缓存命中率为0%
**位置**: `mtpndd_operation_cache.c`, `mtpndd_node.c`

**检查点**:
1. 缓存是否正确初始化？
2. `lookup` 函数的返回值是否正确处理？
3. `store` 后再次 `lookup` 是否能命中？

**建议实验**:
```c
// 在 mtpndd_and_rec 中添加调试
mtpndd_t cached = mtpndd_op_cache_lookup_binary(and_cache, lhs, rhs);
if (cached != MTPNDD_INVALID) {
    MTPNDD_STAT_ADD(cache_lookup_hits, 1);  // 确保这行被执行
    return cached;
}
MTPNDD_STAT_ADD(cache_lookup_misses, 1);
```

#### 4.1.2 增大缓存容量
**当前默认**: 可能过小
**建议**: 根据问题规模动态调整
```c
// nqueens 测试中
config.mtpndd_op_cache_size = 1 << 20;  // 1M entries for n >= 10
```

### 4.2 中优先级 [预期提升: 1.5-3x]

#### 4.2.1 边合并策略优化
**当前**: 收集所有边 -> 排序 -> 合并
**建议**: 使用小规模优化
```c
// 对于小边数，使用简单的线性扫描合并
if (builder->count < 16) {
    // 线性扫描合并，O(n^2) 但常数小
} else {
    // 排序合并，O(n log n)
}
```

#### 4.2.2 减少BDD引用计数操作
**问题**: 每条边的label都需要 ref/deref
**建议**: 批量处理
```c
// 延迟deref，在函数返回前统一处理
// 或使用局部缓存避免重复ref相同的BDD
```

### 4.3 低优先级 [预期提升: 10-30%]

#### 4.3.1 缓存行对齐优化
确保关键数据结构按缓存行对齐：
```c
typedef struct __attribute__((aligned(64))) {
    // ...
} mtpndd_node_record_t;
```

#### 4.3.2 分支预测优化
对热路径添加分支预测提示：
```c
if (__builtin_expect(cached != MTPNDD_INVALID, 1)) {
    return cached;
}
```

---

## 5. 验证实验建议

### 5.1 操作缓存验证
```c
// 添加测试用例验证缓存行为
void test_cache_effectiveness() {
    mtpndd_t a = ..., b = ...;
    mtpndd_t r1 = mtpndd_and(a, b);
    mtpndd_t r2 = mtpndd_and(a, b);  // 应该命中缓存
    assert(r1 == r2);
    // 检查统计: hits应该增加1
}
```

### 5.2 性能剖析
```bash
# 使用perf进行性能分析
perf record ./mtpndd_nqueens_test 10
perf report

# 或使用valgrind的callgrind
valgrind --tool=callgrind ./mtpndd_nqueens_test 10
kcachegrind callgrind.out.*
```

### 5.3 A/B测试
1. 修复操作缓存后对比
2. 边合并策略优化后对比
3. BDD操作批量化后对比

---

## 6. 总结

### 6.1 主要问题（已确认）

#### 6.1.1 时间分解分析 (nqueens_exp N=10)

| 操作 | 时间(s) | 调用次数 | 占比 |
|------|--------|---------|------|
| **cell_and** | **2.473** | 100 | **93%** |
| imp_and | 0.130 | 2940 | 5% |
| imp_or | 0.027 | 2940 | 1% |
| row_or | 0.016 | 100 | <1% |

**核心瓶颈**：`cell_and`（每个cell的implication与formula的AND）占用93%的时间！

这是 `mtpndd_and(formula, cell_implication)` 的调用。随着formula变大，每次操作变得越来越慢（p50=22.9ms, max=70.3ms）。

#### 6.1.2 根因分析

1. **操作缓存命中率过低（13%）** ⚠️ 已确认
   - 根因：直接映射缓存策略，无冲突处理
   - 位置：`mtpndd_operation_cache.c:109-121`
   - **影响**：大型AND操作需要大量子操作，缓存未命中导致重复计算
2. **边构建器开销** - 批量处理策略不如即时合并高效
3. **BDD操作开销** - 底层BDD操作次数过多

### 6.2 预期优化效果
| 优化项 | 预期提升 | 实现难度 | 状态 |
|--------|---------|---------|------|
| 改进缓存策略（组相联/探测） | 3-5x | 中 | **待实现** |
| 增大缓存容量（临时方案） | 1.5-2x | 低 | 可快速验证 |
| 优化边合并 | 1.5-2x | 中 | 待评估 |
| 批量BDD操作 | 1.2-1.5x | 高 | 待评估 |
| 总体 | **5-15x** | - | - |

### 6.3 目标
优化后C实现应该能够：
- 达到或超过Java NDD的性能
- N=12时从66秒降低到**<15秒**（与Java相当）
- 缓存命中率提升到**50%以上**

### 6.4 下一步行动
1. **快速验证**：增大缓存容量到4M，验证是否能提升命中率
2. **中期优化**：实现2-way或4-way组相联缓存
3. **性能剖析**：使用perf确认其他热点

---

## 附录：相关代码位置

| 模块 | 文件 | 关键函数 |
|------|------|---------|
| 操作缓存 | `mtpndd_operation_cache.c` | `lookup_binary`, `store_binary` |
| AND操作 | `mtpndd_node.c:40` | `mtpndd_and_rec` |
| OR操作 | `mtpndd_node.c:164` | `mtpndd_or_rec` |
| 边构建 | `mtpndd_edge_builder.c:98` | `mtpndd_edge_builder_finalize` |
| 节点创建 | `mtpndd_nodetable.c:560` | `mtpndd_mk` |
| GC | `mtpndd_nodetable.c:401` | `mtpndd_gc_collect` |
| 测试 | `test/nqueens.c` | `main` |

---

## 追加：与 Java 参考实现的差异&主要变慢原因

### 1. 关键实现差异
- **数据结构**：C 版使用固定桶数组（边映射/节点表）+ slab 池；Java 版 `HashMap` 随负载自动扩容。C 版默认桶数不足时碰撞严重，遍历成本高。
- **GC/保护策略**：C 版 `gc_protect` 是固定桶的哈希集合；每次公共逻辑操作都会清空全部桶。Java 版 `temporarilyProtect` 只是小 `HashSet`，`clear()` 与元素数量相关。
- **归约规则**：Java `NodeTable.mk` 会在“仅一条 TRUE 边”时直接返回子节点；C 版试图做同样优化，但实现错误导致大量可消除节点被保留。
- **引用计数与释放**：Java 在 GC 时对所有边的子节点减引用；C 版释放节点时遗漏了每个桶的第一个边项（只从 `head->next` 开始），导致子节点 ref 没减，节点表膨胀。
- **并行/串行构建**：NQueens Java 版的逻辑操作都走 JSylvan 并重用缓存；当前分支的 `test/nqueens.c` 完全串行地构造公式，且每步都会触发上述保护清空。

### 2. 已确认的变慢主因（需对齐 Java 行为）
- **节点无法回收（ref 泄漏）**：`mtpndd_release_node` 只对 `head->next` 迭代，桶头边未减引用；子节点永久保活，GC 难以回收，节点表越滚越大。位置：`sylvan/src/sylvan/mtpndd/mtpndd_nodetable.c:404-419`。
- **每次 and/or/not 都 O(桶数) 清空保护集**：`mtpndd_gc_protect_clear()` 遍历 1k/64k 桶并释放 entry；在 NQueens 大量调用下成为固定高成本。位置：`mtpndd_node.c:765-807` 调用，宏定义 `mtpndd_common.h:293-310`。
- **单边 TRUE 归约失效**：`mtpndd_mk` 检测唯一边时使用 `head->next`，实际永远找不到正确的唯一边，无法像 Java 那样直接返回子节点，产生大量冗余节点。位置：`mtpndd_nodetable.c:145-158`。
- **哈希桶固定且可能偏小**：边表和节点表的桶数只在初始化/手动 grow 时调整；碰撞会让 `mtpndd_add_edge`、`nodetable_edges_equal` 退化为链表全扫。Java HashMap 会自动扩容、负载因子控制。
- **额外开销**：C 版在串行路径使用大量原子操作与 BDD ref/deref；`satcount` 必须先把整棵 NDD 转成 MTBDD 再计数，缺少 Java 版的直接计数路径。

这些差异/问题是后续优化的直接目标：修正 refcount 释放、按需清理保护集、恢复单边归约、引入自适应桶或加桶调优，并减少无谓原子/转换开销，以使 C 版更贴近 Java 参考实现的性能。

### 3. GC Protect 设计与使用梳理
- **结构**：`mtpndd_gc_protect_t` 由固定桶数组 + slab entry 组成，新增 `used_bucket_indices` 仅跟踪非空桶，清理时只遍历被占用的桶；统计 `gc_protect_count` 记录保护中的节点数。实现文件：`mtpndd_common.h/c`。
- **写入路径**：在 `and/or/not/diff/exist` 等操作的递归结果创建后调用 `mtpndd_gc_protect_add`（`mtpndd_node.c` 多处）；GC root 收集时遍历 `gcProtect` 并 `ref` 后放入根列表（`mtpndd_nodetable.c:338+`）。
- **清理路径**：每个公开操作入口先调用 `mtpndd_gc_protect_clear`，使用 `used_bucket_indices` 仅清理实际用到的桶；`mtpndd_quit` 也调用一次。删除了未使用的 `contains` 接口，减少无意义遍历。
- **改进方向**：保持 HashSet 行为：新增路径已是 O(1) 插入/查找，清理已经缩到 O(used bucket)；后续可按 Java 行为进一步优化：在递归结束处批量清空（避免多次清空）、将 `used_bucket_indices` 容量与操作深度对齐、在调试构建下提供轻量计数断言取代原子操作。
