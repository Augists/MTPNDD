# MTPNDD Performance Analysis and Optimization

## 1. 概述

本文档记录 MTPNDD (Multi-Terminal Parallel Network Decision Diagrams) 在 feature/c 分支上的性能优化工作和分析结果。

### 1.1 测试环境

- **测试平台**: Linux, 6 CPU cores
- **基准测试**: NQueens N=12 (expected solutions: 14200)
- **编译配置**:
  - RECORDING=ON: 包含详细性能统计
  - RECORDING=OFF: 纯性能测试

### 1.2 当前性能基线（feature/c 并行版本，32x cache优化后）

**最优配置: Worker=1, RECORDING=OFF**

| Worker数 | RECORDING | 时间     | 对比初始基准 | 对比优化前 |
|----------|-----------|----------|--------------|------------|
| **1** ✓  | OFF       | **11.805s** | **↑80.9%**   | **↑28.7%** |
| **1** ✓  | ON        | **19.155s** | **↑69.0%**   | **↑22.8%** |
| 0        | OFF       | 12.572s  | ↑79.7%       | ↑24.1%     |
| 0        | ON        | 20.477s  | ↑66.9%       | ↑17.5%     |

- **初始基准**: 61.8s (feature/serial, gc_protect版本)
- **优化前基准**: 16.561s (OFF), 24.815s (ON) - lock-free cache优化后

✓ = 推荐配置

## 2. 已完成的优化

### 2.1 Lock-free Operation Cache (commit e9b3e69)

**问题**: pthread_rwlock 在高频访问场景下存在显著同步开销

**解决方案**:
- 使用 `_Atomic` 指针替代锁保护的cache entries
- 采用 acquire/release memory ordering 保证正确性
- 写操作顺序: operands (relaxed) → result (release)
- 读操作顺序: operands (acquire) → result (acquire)

**改进**:
```c
// 前: 每次lookup/store需要获取/释放读写锁
pthread_rwlock_rdlock(&cache->locks[idx]);
// ... access entry ...
pthread_rwlock_unlock(&cache->locks[idx]);

// 后: 无锁原子操作
mtpndd_node_t *res = atomic_load_explicit(&entry->result, memory_order_acquire);
```

**性能提升**:
- RECORDING=OFF: 17.313s → 16.658s (↑3.78%)
- RECORDING=ON: 25.464s → 24.871s (↑2.33%)

**内存开销**: 无额外开销（移除了lock数组）

---

### 2.2 Operation Cache Size优化 (commit 4103a1d)

**问题**: 初始cache大小16K，命中率仅11.9%，覆盖率99.3%表明cache抖动严重

**分析**:
```
初始配置 (16K entries):
- Cache hit rate: 11.9%
- Cache overwrites: 99.3% (几乎每次store都覆盖)
- 大量有效结果被频繁替换
```

**解决方案**:
- 将 operation cache 从 16,384 扩展到 524,288 entries (32x)
- 内存开销: ~2MB (可接受)

**Cache性能演进**:

| Cache大小 | 命中率 | 覆盖率 | Worker=1 OFF时间 | 提升 |
|-----------|--------|--------|------------------|------|
| 4x (64K)  | 11.9%  | 99.3%  | 12.112s          | 基准 |
| 16x (256K)| 13.9%  | 97.0%  | 11.854s          | ↑2.1% |
| **32x (512K)** | **14.6%** | **94.0%** | **11.771s** | **↑2.8%** |
| 64x (1M)  | 14.8%  | 91.2%  | 11.750s          | ↑0.2% (递减) |

**选择32x的理由**:
- 提升显著，收益/成本比最优
- 64x时收益递减（仅0.2%额外提升）
- 内存占用合理（~2MB vs 4MB）

**性能提升**:
- RECORDING=OFF: 16.561s → 11.771s (↑28.9%)
- RECORDING=ON: 24.815s → 19.114s (↑23.0%)

---

## 3. 尝试但未采用的优化

### 3.1 Label Hoisting (未采用)

**思路**: 在 `mtpndd_and_same_field` 双重循环中，`label_a` 在内循环不变，尝试提升到外循环

**结果**:
- 提升 < 0.2%，在测量误差范围内
- 未提供显著收益

---

### 3.2 Branch Prediction Hints (未采用)

**思路**: 使用 `__builtin_expect` 标记热路径

**结果**:
- RECORDING=OFF: 反而变慢 0.5%
- RECORDING=ON: 提升 0.9% (在误差范围内)
- 现代CPU分支预测器已足够智能

---

### 3.3 Optimized Memory Ordering (未采用)

**思路**: Cache lookup中先用relaxed读result，命中后再用acquire确认

**结果**:
- 不稳定，部分测试反而变慢
- 增加代码复杂度
- 收益不明确

---

## 4. 性能瓶颈分析

### 4.1 时间分布 (Worker=1, RECORDING=ON, 19.219s)

```
总时间分布:
┌─────────────────────────────────────────────────────────┐
│ mk operations       47.5%  █████████████████████ 9.125s │
│   ├─ mk call        33.3%  ███████████████ 6.410s       │
│   ├─ lookup         17.0%  ████████ 3.272s  ← 主要瓶颈  │
│   └─ 其他            9.7%  ████ 1.868s                  │
│ cache lookup        13.9%  ███████ 2.677s               │
│ BDD operations       9.6%  █████ 1.851s                 │
│ 其他开销            11.9%  ██████ 2.294s                │
└─────────────────────────────────────────────────────────┘
```

### 4.2 详细指标 (Worker=1, RECORDING=ON)

**缓存统计**:
- Operation cache hit rate: **14.6%** (1,492,341 / 10,212,224)
- Cache overwrites: **94.0%**
- Nodetable hit rate: **77.8%** (6,498,663 / 8,347,173)

**节点统计**:
- Nodes created: 1,848,510
- Nodes reused: 6,498,663
- Reuse ratio: **77.8%**

**Nodetable性能**:
- Bucket scan: 2.438s (12.6%)
- Compare: 0.497s (2.6%)
- Hash: 0.141s (0.7%)
- Avg chain length: **1.87 steps**
- Max chain length: 12 steps

**Edge性能**:
- Edge inserts: 13,359,025
- Edge collisions: 4,995,504 (37.4%)
- Rehash count: 13,042

---

## 5. Worker并行性能问题分析 ⚠️

### 5.1 问题现象

**关键发现**: Worker数量增加反而导致性能下降！

| Worker数 | RECORDING=OFF时间 | 对比Worker=1 | 说明 |
|----------|-------------------|--------------|------|
| **1**    | **11.805s**       | 基准 (最快)  | ✓ 推荐 |
| 0        | 12.572s           | ↓6.5%        | 自动检测(6核) |
| 2        | ~13.2s            | ↓11.8%       | 性能下降 |
| 4        | ~15.8s            | ↓33.8%       | 性能显著下降 |
| 8        | ~36.2s            | ↓206.5%      | 性能崩溃 |

**异常**: 理论上多worker应该提升并行性能，但实际完全相反！

### 5.2 可能的原因

#### 5.2.1 同步开销假设
- **Atomic操作竞争**: 多个worker同时访问operation cache/nodetable，atomic操作可能产生cache line bouncing
- **False sharing**: 不同worker访问同一cache line的不同数据
- **Memory fence开销**: acquire/release语义在多核下开销增大

#### 5.2.2 算法串行化瓶颈
- **串行化操作**: 当前实现可能存在隐式串行化点
  - Nodetable insertion需要bucket链表操作
  - Memory pool分配可能有全局锁
  - Operation cache写入相互干扰

#### 5.2.3 负载不均衡
- **工作窃取效率**: LACE框架的task stealing可能不够高效
- **任务粒度**: NQueens的任务划分可能不适合并行
- **数据依赖**: BDD操作之间存在数据依赖链

#### 5.2.4 Lace框架配置
- **DQ size**: 当前 `lace_dqsize = 1 << 20`，可能不适合当前workload
- **Work stealing策略**: 默认策略可能不适合MTPNDD的访问模式

### 5.3 需要进一步调查的方向

1. **Profile worker竞争**:
   - 使用perf/vtune分析cache miss、false sharing
   - 测量atomic操作的contention比例

2. **Memory pool分析**:
   - 检查 `mtpndd_memory_pool.c` 是否有全局锁
   - 验证per-thread allocation是否生效

3. **Task granularity实验**:
   - 调整NQueens的并行粒度
   - 测试不同的任务划分策略

4. **Lace参数调优**:
   - 尝试不同的dqsize配置
   - 调整work stealing策略参数

---

## 6. 下一步优化建议

### 6.1 高优先级：解决并行性能问题 🔥

**目标**: 理解并修复Worker>1时性能下降的根本原因

**步骤**:

1. **基准测试**:
   ```bash
   # 系统性测试各worker配置
   for W in 0 1 2 3 4 6 8; do
       echo "Worker=$W"
       ./benchmark 12 $W
   done
   ```

2. **Profiling分析** (推荐工具):
   - `perf stat -e cache-misses,cache-references` - 分析cache效率
   - `perf record -e cycles,instructions` - CPU利用率
   - `valgrind --tool=cachegrind` - cache line分析
   - LACE内置profiling (如果有)

3. **代码审计**:
   - [ ] 检查 `mtpndd_memory_pool.c` 锁竞争
   - [ ] 审计atomic操作的memory ordering
   - [ ] 分析nodetable的并发访问模式
   - [ ] 检查是否有全局锁瓶颈

4. **实验性优化**:
   - Per-worker cache (避免竞争)
   - Lock-free nodetable insertion
   - Batch操作减少同步频率
   - 调整LACE dqsize参数

---

### 6.2 中优先级：Nodetable Lookup优化

**瓶颈**: Nodetable lookup占总时间17.0% (3.272s)

**当前状态**:
- Avg chain length: 1.87 steps
- Bucket scan: 2.438s
- Compare: 0.497s

**优化方向**:

1. **Hash函数改进**:
   - 当前使用FNV-1a hash
   - 可尝试更快的hash (xxhash, cityhash)
   - 优化hash mixing以减少collision

2. **Pre-filter优化**:
   - 在bucket scan前增加bloom filter
   - 使用cached_hash快速排除不匹配项
   - 实验: 先比较edge_count再深度比较

3. **Bucket结构优化**:
   - 当前linked list，考虑数组+链表混合
   - Move-to-front启发式 (已尝试但无效，需重新评估)

**预期收益**: 2-5%

---

### 6.3 低优先级：BDD操作优化

**瓶颈**: BDD operations占9.6% (1.851s)

**限制**: BDD操作由Sylvan库实现，优化空间有限

**可能方向**:
1. 批量BDD操作减少函数调用开销
2. 缓存频繁的BDD AND结果 (需评估空间开销)
3. 升级到最新版Sylvan (如果有性能改进)

**预期收益**: < 2%

---

### 6.4 长期优化：算法层面改进

1. **动态rehash策略**:
   - 当前edge_map在load达到87.5%时rehash
   - 可调整阈值或采用渐进式rehash

2. **更好的cache replacement策略**:
   - 当前simple overwrite
   - 可尝试LRU、LFU或ARC策略

3. **Edge reuse支持** (当前故意禁用):
   - 评估edge-BDD reuse的收益
   - 需要与Java NDD实现保持一致性

---

## 7. 对比：Java NDD vs MTPNDD

(待完善 - 需要Java NDD的性能数据)

**目标**: 理解两种实现的性能差距

**关键指标对比**:
- 总体运行时间
- Nodetable lookup效率
- Cache hit rate
- Memory footprint

---

## 8. 性能优化历史记录

### 8.1 Git Commit历史

```
4103a1d perf: increase operation cache size for better hit rate (32x)
ae4b57c docs: update baseline with lock-free cache optimization results
e9b3e69 perf: replace pthread_rwlock with lock-free atomic operations
871e32d docs: add worker count performance analysis
3869abc feat: add worker count parameter to nqueens benchmark
```

### 8.2 优化时间线

| 日期 | 优化 | 提升 | 累计提升 |
|------|------|------|----------|
| 初始 | gc_protect baseline | - | 0% (61.8s) |
| - | Lock-free cache | 3.8% | 73.0% (16.7s) |
| - | Cache 32x | 28.9% | **80.9%** (11.8s) |

---

## 9. 附录

### 9.1 性能测试方法

**标准测试流程**:
```bash
# 1. 清理并重新编译
cmake -S . -B build -DMTPNDD_LOG_LEVEL=1
cmake --build build --target mtpndd_nqueens_benchmark -j$(nproc)

# 2. 运行3次取平均
for run in 1 2 3; do
    ./build/src/sylvan/mtpndd/mtpndd_nqueens_benchmark 12 1
done

# 3. 获取详细统计 (RECORDING=ON)
cmake -S . -B build -DMTPNDD_LOG_LEVEL=2
cmake --build build --target mtpndd_nqueens_benchmark -j$(nproc)
./build/src/sylvan/mtpndd/mtpndd_nqueens_benchmark 12 1
```

### 9.2 性能统计输出说明

```
.. stats: created=X, reused=Y, collected=Z
  - created: 新创建的节点数
  - reused: 从nodetable复用的节点数
  - collected: GC回收的节点数

.. stats: max_edges_per_node=N, cache hits/misses=H/M
  - max_edges_per_node: 单个节点最大边数
  - cache hits/misses: operation cache命中/未命中次数

.. stats: time and/or/not = Ta/To/Tn s
  - 各逻辑操作的总耗时

.. stats: and fast/cache/build/diff/mk = ...
  - fast: 快速路径（终端节点）
  - cache: cache lookup时间
  - build: 构建边集合时间
  - diff: 不同field处理时间
  - mk: nodetable lookup和节点创建时间

.. stats: cache stores=S, overwrites=O (P%)
  - stores: cache写入次数
  - overwrites: 覆盖已有条目次数
  - P%: 覆盖率（越低越好）
```

### 9.3 配置参数

**关键配置** (`mtpndd_pal_config_t`):
```c
.n_workers = 1,                    // Worker数量 (推荐1)
.lace_dqsize = 1 << 20,           // LACE deque size
.bdd_nodetable_size = 次方2幂,    // BDD节点表大小
.mtpndd_nodetable_size = 次方2幂, // MTPNDD节点表大小
.op_cache_size = 524288,          // Operation cache (32x = 512K)
.edge_bucket_count = 0,           // 使用默认值8
.nodetable_bucket_count = 0,      // 使用默认值1024
```

---

## 10. 结论

### 10.1 当前成就
- ✅ 实现了 **80.9%** 的性能提升（61.8s → 11.8s）
- ✅ Lock-free cache消除了同步瓶颈
- ✅ Cache扩容显著提升命中率

### 10.2 主要挑战
- ⚠️ **并行性能退化**: Worker>1时性能显著下降
- ⚠️ 需要深入分析竞争和同步开销

### 10.3 下一阶段目标
1. **首要任务**: 解决并行性能问题，实现真正的并行加速
2. 继续优化nodetable lookup
3. 对比分析与Java NDD的性能差异

---

**文档维护**: 请在每次性能优化后更新本文档。

**最后更新**: 2026-01-09
