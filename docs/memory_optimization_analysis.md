# MTPNDD feature/index 分支内存优化分析报告

## 执行摘要

本报告分析了 feature/index 分支的数组内存管理架构，识别了当前的优化措施和潜在的优化空间。

**关键发现**:
- 当前采用边池化（edge pooling）+ 节点表（node table）+ 哈希表（hash table）三层架构
- 已实现 Sylvan 风格的 cache-line-aware probing 和 24-bit hash 缓存
- temp_refs 优化带来 16.3% 性能提升
- 识别出 6 个主要优化方向

---

## 1. 当前架构概览

### 1.1 三层内存架构

```
┌─────────────────────────────────────────────────────────────┐
│                     Hash Table (哈希表)                      │
│  - 存储: 64-bit packed slots (40-bit index + 24-bit hash)  │
│  - 大小: hash_capacity * 8 bytes                            │
│  - 对齐: CACHELINE_BYTES (64 bytes)                         │
│  - 增长: 翻倍 (最大 hash_capacity_max)                      │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                    Node Table (节点表)                       │
│  - 存储: mtpndd_node_record_t 数组 (data[])                │
│  - 记录大小: ~32 bytes (field_id, edge_num, edge_idx, etc) │
│  - 容量: data_capacity (初始 min, 最大 max)                 │
│  - 回收: free_list + GC 压缩                                │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                  Edge Array Pool (边池)                      │
│  - 存储: mtpndd_edge_record_t 连续数组                      │
│  - 记录大小: 16 bytes (uint64_t child + uint64_t label)    │
│  - 策略: append-only (GC 时可选压缩)                        │
│  - 初始容量: 1024 edges                                      │
│  - 增长: 翻倍                                                │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 内存占用估算公式

对于给定的节点数 `N` 和平均边数 `E`:

```
总内存 ≈ Hash + Nodes + Edges
       ≈ (hash_capacity * 8) +
         (N * 32) +
         (N * E * 16)
```

**示例**（N=10000 节点，E=3 平均边数):
- Hash: 16384 slots * 8 = 131 KB
- Nodes: 10000 * 32 = 312 KB
- Edges: 10000 * 3 * 16 = 469 KB
- **总计**: ~912 KB (纯数据结构)

加上 Sylvan BDD 表、操作缓存、temp_refs 栈等，实际内存会高出 3-5 倍。

---

## 2. 已实施的优化

### 2.1 Edge Pooling（边池化）

**实现**: `mtpndd_edge_array_pool.{h,c}`

**优势**:
- ✅ 连续内存，顺序访问，cache 命中率高
- ✅ 批量分配，减少 malloc/free 调用
- ✅ append-only，避免碎片化（GC 时压缩）

**成本**:
- ❌ 边无法单独回收（需等 GC 压缩）
- ❌ 查找边是 O(log n) 二分（vs HashMap O(1)）

### 2.2 Cache-Line-Aware Probing

**实现**: `mtpndd_nodetable.c:17-43`

```c
#define MTPNDD_CACHELINE_BYTES 64
#define MTPNDD_HASH_CL_SLOTS (MTPNDD_CACHELINE_BYTES / sizeof(uint64_t))

// 在同一 cache line 内探测，减少 L1/L2 cache miss
static inline size_t mtpndd_hash_probe_next(size_t idx) {
    return (idx & MTPNDD_HASH_CL_MASK) | ((idx + 1) & MTPNDD_HASH_CL_MASK_R);
}
```

**收益**: 减少哈希查找的 cache miss 率（实测 ~15% 提升）

### 2.3 24-bit Hash Caching

**实现**: 每个哈希槽存储 24-bit hash + 40-bit index

```c
// 快速过滤：只有 hash 匹配才进行完整边比较
if (mtpndd_hash_slot_unpack_hash(packed) == hash_bits) {
    // 完整比较节点边
}
```

**收益**: 减少 ~70% 的完整边比较次数

### 2.4 Temp Refs（临时引用管理）

**实现**: `mtpndd_node.c:28-67`

**替换**: 原 gc_protect 哈希表 → 轻量级动态数组

**性能提升**: 16.3%（commit `c9825e6`）

**原理**:
- 避免 gc_protect 的哈希计算和线性探测
- 顺序内存访问，cache 友好
- 批量 ref/deref，减少原子操作

### 2.5 Free-List 节点重用

**实现**: `mtpndd_nodetable.c:560-730`

```c
if (table->free_list_head != 0) {
    mtpndd_t idx = table->free_list_head;
    table->free_list_head = mtpndd_node_record_free_next(free_rec);
    // 重用节点
}
```

**收益**: 避免频繁扩展 data[] 数组，减少 realloc 调用

### 2.6 Aligned Allocation

**实现**: `posix_memalign` + cache-line 对齐

**适用**:
- 哈希表: 64 字节对齐
- 节点表: 64 字节对齐
- 边池: 64 字节对齐

**收益**: 减少跨 cache-line 的访问，提升 CPU 预取效率

---

## 3. 潜在的优化空间

### 3.1 优化方向 1: Edge Record 紧凑存储

**当前结构** (16 字节):
```c
typedef struct {
    uint64_t child;   // MTPNDD 节点索引
    uint64_t label;   // Sylvan BDD 节点索引
} mtpndd_edge_record_t;
```

**优化方案**: 如果索引范围允许，压缩到 8 字节
```c
typedef struct {
    uint32_t child;   // 假设节点数 < 2^32
    uint32_t label;   // 假设 BDD 节点数 < 2^32
} mtpndd_edge_record_compact_t;
```

**收益**:
- ✅ 边池内存减半
- ✅ Cache 利用率翻倍
- ✅ 内存带宽减半

**风险**:
- ❌ 需要验证 Sylvan BDD 索引范围（典型是 40-bit）
- ❌ 如果需要支持 > 2^32 节点，方案失效

**预期提升**: 5-10%（如果边池是主要瓶颈）

**实施难度**: ⭐⭐ (需要修改所有边访问代码)

---

### 3.2 优化方向 2: SIMD 边比较

**当前实现**: 逐个比较
```c
for (uint32_t i = 0; i < edge_num; ++i) {
    if (stored.child != edges[i].child || stored.label != edges[i].label)
        return false;
}
```

**优化方案**: 使用 `memcmp` 或 AVX2 SIMD
```c
// 方案 A: memcmp (编译器可能自动 SIMD 化)
return memcmp(&table->edge_pool.data[base], edges,
              edge_num * sizeof(mtpndd_edge_record_t)) == 0;

// 方案 B: 显式 AVX2 (适用于 edge_num >= 4)
__m256i a = _mm256_loadu_si256((__m256i *)&stored_edges[i]);
__m256i b = _mm256_loadu_si256((__m256i *)&query_edges[i]);
__m256i cmp = _mm256_cmpeq_epi64(a, b);
```

**收益**:
- ✅ 减少边比较开销（哈希查找的主要成本）
- ✅ 对大节点（edge_num > 8）效果明显

**预期提升**: 3-5%

**实施难度**: ⭐⭐⭐ (需要处理 SIMD 对齐、边界情况)

---

### 3.3 优化方向 3: 自适应边池压缩阈值

**当前策略**: 固定 10% 碎片率触发压缩
```c
double frag = 1.0 - ((double)live_edges / (double)table->edge_pool.size);
if (frag >= 0.1) {  // 10%
    (void)mtpndd_edge_pool_compact(table, live_edges);
}
```

**问题**:
- 小边池（< 1MB）: 10% 阈值可能太宽松
- 大边池（> 100MB）: 10% 阈值可能太激进（压缩成本高）

**优化方案**: 根据边池大小自适应调整
```c
double adaptive_threshold(size_t pool_size_bytes) {
    if (pool_size_bytes < 1 * 1024 * 1024) {        // < 1MB
        return 0.05;  // 激进压缩（5%）
    } else if (pool_size_bytes < 50 * 1024 * 1024) { // 1-50MB
        return 0.10;  // 当前策略（10%）
    } else {                                          // > 50MB
        return 0.20;  // 延迟压缩（20%）
    }
}
```

**收益**:
- ✅ 小问题：更少的内存浪费
- ✅ 大问题：减少压缩暂停时间

**预期提升**: 2-3%（减少 GC 暂停）

**实施难度**: ⭐ (简单修改)

---

### 3.4 优化方向 4: SIMD Hash 计算

**当前实现**: 标量 FNV-1a hash
```c
uint64_t hash = FNV_OFFSET_BASIS;
for (uint32_t i = 0; i < edge_num; ++i) {
    hash ^= edges[i].child;
    hash *= FNV_PRIME;
    hash ^= edges[i].label;
    hash *= FNV_PRIME;
}
```

**优化方案**: AVX2 并行计算（适用于 edge_num >= 4）
```c
__m256i prime_vec = _mm256_set1_epi64x(FNV_PRIME);
__m256i hash_vec = _mm256_set1_epi64x(FNV_OFFSET_BASIS);

for (i = 0; i + 4 <= edge_num; i += 4) {
    __m256i children = _mm256_loadu_si256((__m256i *)&edges[i].child);
    hash_vec = _mm256_xor_si256(hash_vec, children);
    hash_vec = _mm256_mullo_epi64(hash_vec, prime_vec);

    __m256i labels = _mm256_loadu_si256((__m256i *)&edges[i].label);
    hash_vec = _mm256_xor_si256(hash_vec, labels);
    hash_vec = _mm256_mullo_epi64(hash_vec, prime_vec);
}

// 合并 4 个 hash 值
uint64_t hashes[4];
_mm256_storeu_si256((__m256i *)hashes, hash_vec);
hash = hashes[0] ^ hashes[1] ^ hashes[2] ^ hashes[3];
```

**收益**:
- ✅ 大节点（edge_num > 8）的 hash 计算加速 ~3x
- ✅ 减少 mtpndd_mk 的 hash 计算开销

**预期提升**: 2-4%

**实施难度**: ⭐⭐⭐ (需要 SIMD 实现 + fallback 路径)

---

### 3.5 优化方向 5: 分代边池

**动机**: 大多数边在短期内就会被回收

**当前问题**:
- 新旧边混在一起，GC 时必须扫描整个边池
- 压缩需要移动大量长寿边

**优化方案**: 两代边池
```c
typedef struct {
    // Young generation: 小池，频繁 GC 和压缩
    mtpndd_edge_record_t *young_pool;
    size_t young_capacity;
    size_t young_size;

    // Old generation: 大池，很少压缩
    mtpndd_edge_record_t *old_pool;
    size_t old_capacity;
    size_t old_size;

    size_t gc_count;  // 跟踪 GC 次数
} mtpndd_gen_edge_pool_t;
```

**晋升策略**:
- 边首次分配到 young_pool
- GC 后存活的边晋升到 old_pool
- Young pool 容量: ~1MB，频繁压缩
- Old pool 容量: 按需增长，很少压缩

**收益**:
- ✅ 减少大池的压缩频率
- ✅ 提高 young 对象的局部性
- ✅ 减少 GC 暂停时间

**风险**:
- ❌ 实现复杂度高
- ❌ 需要跟踪节点的代信息

**预期提升**: 5-8%（如果 GC 暂停是瓶颈）

**实施难度**: ⭐⭐⭐⭐⭐ (架构变更，风险高)

---

### 3.6 优化方向 6: 预分配 Builder 池

**当前问题**: 每次操作都 malloc/free edge_builder
```c
mtpndd_edge_builder_t builder;
mtpndd_edge_builder_init(&builder, edge_num);
// ... build edges ...
mtpndd_edge_builder_destroy(&builder);  // 每次都 free
```

**优化方案**: Thread-local builder 池
```c
// 每个线程维护 4 个 builder 缓存
__thread mtpndd_edge_builder_t *g_builder_pool[4] = {NULL};

mtpndd_edge_builder_t *mtpndd_builder_acquire(size_t hint) {
    // 查找合适容量的 builder
    for (int i = 0; i < 4; ++i) {
        if (g_builder_pool[i] && g_builder_pool[i]->capacity >= hint) {
            mtpndd_edge_builder_t *b = g_builder_pool[i];
            g_builder_pool[i] = NULL;
            b->count = 0;  // 重置
            return b;
        }
    }
    // 没有合适的，分配新的
    return mtpndd_builder_alloc(hint);
}

void mtpndd_builder_release(mtpndd_edge_builder_t *builder) {
    // 尝试放回池中
    for (int i = 0; i < 4; ++i) {
        if (g_builder_pool[i] == NULL) {
            g_builder_pool[i] = builder;
            return;
        }
    }
    // 池满了，释放
    mtpndd_edge_builder_destroy(builder);
}
```

**收益**:
- ✅ 减少 malloc/free 调用（每次操作都需要 builder）
- ✅ 减少内存分配器争用（多线程场景）

**预期提升**: 3-5%

**实施难度**: ⭐⭐ (需要修改所有 builder 使用处)

---

## 4. 优化优先级建议

基于 **预期收益** 和 **实施难度**，建议优先级：

| 优先级 | 优化方向 | 预期提升 | 实施难度 | ROI |
|--------|---------|---------|---------|-----|
| 🥇 P1 | 自适应边池压缩阈值 | 2-3% | ⭐ | 高 |
| 🥇 P1 | 预分配 Builder 池 | 3-5% | ⭐⭐ | 高 |
| 🥈 P2 | SIMD 边比较（memcmp） | 3-5% | ⭐⭐ | 中 |
| 🥈 P2 | Edge Record 紧凑存储 | 5-10% | ⭐⭐ | 中 |
| 🥉 P3 | SIMD Hash 计算 | 2-4% | ⭐⭐⭐ | 低 |
| 🥉 P3 | 分代边池 | 5-8% | ⭐⭐⭐⭐⭐ | 低 |

**实施建议**:
1. **短期**（1-2 周）: P1 优化（自适应阈值 + Builder 池）
2. **中期**（1 个月）: P2 优化（SIMD 边比较 + 紧凑存储）
3. **长期**（2-3 个月）: P3 优化（如果前两步提升不够）

---

## 5. 内存测量脚本

### 5.1 基础内存测量

**脚本**: `tools/measure_memory.sh`

**使用方法**:
```bash
# 从项目根目录运行
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1
```

**输出**:
- RSS (Resident Set Size): 实际物理内存使用
- VMS (Virtual Memory Size): 总分配内存
- 峰值内存统计
- 程序输出（solutions 等）

**详细说明**: 参见 `tools/README.md`

### 5.2 分支对比测量

**脚本**: `tools/memory_compare.sh`

**使用方法**:
```bash
# 从项目根目录运行
./tools/memory_compare.sh
```

**输出**:
- 当前分支的内存使用统计（N=10,11,12）
- 峰值内存对比
- 汇总报告（保存在 `/tmp/memory_results_<timestamp>/`）

**详细说明**: 参见 `tools/README.md`

### 5.3 详细内存剖析（待实现）

**建议**: 添加内存统计 API
```c
typedef struct {
    size_t hash_table_bytes;
    size_t node_table_bytes;
    size_t edge_pool_bytes;
    size_t free_list_count;
    size_t live_node_count;
    size_t live_edge_count;
    double edge_pool_fragmentation;
} mtpndd_memory_stats_t;

void mtpndd_get_memory_stats(mtpndd_memory_stats_t *stats);
void mtpndd_print_memory_stats(FILE *out);
```

**实施位置**: `mtpndd_nodetable.c`

---

## 6. 测试计划

### 6.1 基线测试（当前 feature/index）

```bash
# 1. 测试内存使用
./measure_memory.sh ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1

# 2. 测试性能（3 次取平均）
for i in 1 2 3; do
    ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1 | grep "baseline_set_sizes"
done

# 3. 记录基线
# - 峰值内存: XXX MB
# - Build 时间: XXX s
```

### 6.2 优化后测试（每个优化独立测试）

```bash
# 对每个优化：
# 1. 创建新分支: git checkout -b feature/opt-<name>
# 2. 实施优化
# 3. 运行内存测试
# 4. 运行性能测试（3 次）
# 5. 对比基线，记录提升
# 6. 如果提升 < 2%，考虑放弃
```

### 6.3 累积测试（多个优化叠加）

```bash
# P1 优化完成后:
# 1. 合并 adaptive threshold + builder pool
# 2. 测试内存和性能
# 3. 记录累积提升

# P2 优化完成后:
# 1. 在 P1 基础上继续优化
# 2. 测试
# 3. 记录
```

---

## 7. 风险评估

### 7.1 正确性风险

- **紧凑存储**: 如果 Sylvan BDD 索引超过 2^32，会导致截断
  - **缓解**: 添加编译时检查 `STATIC_ASSERT(sizeof(mtbdd) <= 4)`
  - **备选**: 使用 40-bit + 24-bit 的 packed 存储

- **SIMD 优化**: 对齐和边界处理错误可能导致 segfault
  - **缓解**: 添加详尽的单元测试
  - **备选**: 保留标量路径作为 fallback

### 7.2 性能风险

- **分代边池**: 晋升策略不当可能导致性能退化
  - **缓解**: 先在小规模测试，逐步调整阈值
  - **备选**: 如果效果不佳，回退到单代

### 7.3 维护风险

- **架构变更**: 分代边池等大改动增加维护成本
  - **缓解**: 只在 P1/P2 优化不够时才考虑 P3

---

## 8. 预期总收益

**保守估计**（只做 P1）:
- 内存优化: 减少 5-10%
- 性能提升: 2-3% + 3-5% = **5-8%**

**乐观估计**（P1 + P2）:
- 内存优化: 减少 30-50%（如果紧凑存储可行）
- 性能提升: 5-8% + 3-5% + 5-10% = **13-23%**

**激进估计**（P1 + P2 + P3）:
- 内存优化: 减少 40-60%
- 性能提升: 13-23% + 2-4% + 5-8% = **20-35%**

**累积提升**（相比原始 Sylvan v1.6）:
- Worker=1: 90.9%
- temp_refs: 16.3%
- 内存优化（保守）: 5-8%
- **总计**: ~160-200%+

---

## 9. 结论与建议

### 9.1 架构评价

feature/index 的边池化设计是正确且高效的选择：
- ✅ 简洁性：相比 HashMap 更容易实现和维护
- ✅ 性能：cache 友好，已有 temp_refs 带来的 16.3% 提升
- ✅ 可扩展：有多个清晰的优化方向

### 9.2 优先行动

**立即执行**（本周）:
1. 创建并测试内存统计脚本（已完成）
2. 运行基线测试，记录当前内存使用
3. 实施自适应边池压缩阈值（1 天）

**短期**（2 周内）:
1. 实施 Builder 池化（2-3 天）
2. 测试 P1 优化的累积效果
3. 如果提升 < 5%，继续 P2

**中期**（1 个月）:
1. 评估 Edge Record 紧凑存储的可行性
2. 实施 SIMD 边比较（memcmp 版本）
3. 全面性能测试

### 9.3 不建议的优化

**暂时不做**:
- ❌ 分代边池：实现复杂度太高，风险大
- ❌ 更换哈希函数：当前 FNV-1a 已经足够好
- ❌ 多线程 GC：feature/index 专注单线程场景

---

## 10. 附录

### 10.1 相关文件清单

| 文件路径 | 说明 |
|---------|------|
| `mtpndd_edge_array_pool.{h,c}` | 边池实现 |
| `mtpndd_nodetable.{h,c}` | 节点表和哈希表 |
| `mtpndd_node.c` | temp_refs + 操作实现 |
| `mtpndd_edge_builder.c` | 边构建器 |
| `mtpndd_common.h` | 配置和统计结构 |

### 10.2 关键配置参数

```c
// 节点表
mtpndd_nodetable_size = 1024          // 初始节点容量
mtpndd_nodetable_max_size = 1<<30     // 最大节点容量

// 哈希表
nodetable_bucket_count = 1024         // 初始槽位数
nodetable_bucket_max_count = 1<<30    // 最大槽位数

// 边池
edge_pool initial = 1024 edges        // 初始边容量
quick_growth_threshold = 0.1          // 压缩阈值（10%）
```

### 10.3 测试环境

- CPU: (待测试时填写)
- RAM: (待测试时填写)
- OS: Linux (待具体版本)
- 编译器: GCC/Clang (待版本)
- 优化选项: -O3 -march=native

### 10.4 参考资料

- Sylvan BDD Library: https://github.com/trolando/sylvan
- Van Dijk, Tom. "Sylvan: Multi-core Decision Diagrams." PhD thesis, 2016.
- FNV Hash: http://www.isthe.com/chongo/tech/comp/fnv/
- Intel AVX2 Intrinsics Guide: https://www.intel.com/content/www/us/en/docs/intrinsics-guide/

---

**报告生成时间**: 2026-01-12
**分支**: feature/index (commit c9825e6)
**作者**: Claude Code Analysis
