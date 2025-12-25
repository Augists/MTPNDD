# MTPNDD 优化分析

本文档分析了不同分支的架构特点，以及各种优化方案的适用性。

---

## 1. 分支架构对比

### 1.1 数据结构差异

| 特性 | feature/c / feature/serial | feature/index |
|------|---------------------------|---------------|
| **节点表示** | 指针 (`mtpndd_node_t *`) | 索引 (`mtpndd_t = uint64_t`) |
| **Edge 存储** | HashMap (每个节点独立的 bucket) | 连续数组 (`edge_array_pool`) |
| **Hash 表** | 链表法 (bucket + entry) | 开放寻址法 (Sylvan-style) |
| **Hash 查找** | 遍历 edge map 计算 hash | 直接用 `mtpndd_node_struct_hash` |
| **节点回收** | 内存池 + 链表 | Free-list (重用索引) |
| **Edge 查找** | HashMap O(1) 平均 | 二分查找 O(log n) |

### 1.2 feature/index 的 Sylvan-style 优化

feature/index 已经内置了多项来自 Sylvan 的优化：

#### 24-bit hash 存储在 slot 中用于快速过滤

```c
#define MTPNDD_NODETABLE_SLOT_MASK_HASH  ((uint64_t)0xffffff0000000000ULL)

// 在查找时，先比较 hash bits 再比较实际内容
if (mtpndd_hash_slot_unpack_hash(packed) == hash_bits) {
    // 只有 hash 匹配才进行完整比较
}
```

这避免了大多数不必要的完整比较。

#### Cache-line-aware probing

```c
#define MTPNDD_HASH_CL_SLOTS (MTPNDD_CACHELINE_BYTES / sizeof(uint64_t))

static inline size_t mtpndd_hash_probe_next(size_t idx) {
    return (idx & MTPNDD_HASH_CL_MASK) | ((idx + 1) & MTPNDD_HASH_CL_MASK_R);
}
```

在同一 cache line 内连续探测，然后跳到下一个 cache line。

#### 动态 probe threshold 调整

```c
static inline size_t mtpndd_hash_probe_threshold(size_t table_size) {
    size_t threshold = 192u - 2u * (size_t)__builtin_clzll(v);
    // ...
}
```

根据表大小动态调整探测阈值。

---

## 2. 优化方案分析

### 2.1 Cached Hash 优化

**在 feature/c / feature/serial 中**：

由于每个节点的 edge 存储在 HashMap 中，计算 hash 需要遍历所有 bucket：

```c
// O(edge_count) 遍历
for (size_t i = 0; i < edge_bucket_cnt; i++) {
    edge_bucket_entry_t *entry = key->buckets[i];
    while (entry) {
        // 计算每个 entry 的 hash
        entry = entry->next;
    }
}
```

缓存 hash 值可以将查找复杂度从 O(n) 降到 O(1)。

**在 feature/index 中**：

Edge 存储在连续数组中，遍历本身就是 cache-friendly 的：

```c
for (uint32_t i = 0; i < edge_num; ++i) {
    h ^= (uint64_t)edges[i].child;  // 连续内存访问
    h *= 1099511628211ULL;
    h ^= (uint64_t)edges[i].label;
    h *= 1099511628211ULL;
}
```

**结论**：Cached Hash 优化在 feature/index 中收益有限，不建议引入。

### 2.2 memset 初始化优化

**在 feature/c / feature/serial 中**：

需要初始化 bucket 数组：

```c
memset(dest->buckets, 0, sizeof(edge_bucket_entry_t *) * bucket_cnt);
```

**在 feature/index 中**：

使用 `mtpndd_edge_builder_t` 构建，无 bucket 数组。`mtpndd_aligned_zalloc` 已使用 `memset`。

**结论**：不适用于 feature/index。

---

## 3. feature/index 的潜在新优化

### 3.1 Edge Builder 预排序插入

**动机**：当前 `mtpndd_edge_builder_finalize` 会对 edges 排序（O(n log n)）并合并重复项。

**当前流程**：
```c
// 1. 多次 push（无序）
mtpndd_edge_builder_push(&builder, child, label);

// 2. finalize 时排序
mtpndd_edge_builder_finalize(&builder);  // qsort O(n log n)
```

**优化方案**：使用有序插入代替排序
```c
// 有序插入，O(n) 二分查找 + O(n) 移动
bool mtpndd_edge_builder_push_sorted(mtpndd_edge_builder_t *builder,
                                     mtpndd_t child,
                                     mtpndd_bdd_t label) {
    // 二分查找插入位置
    size_t pos = binary_search(builder->edges, builder->count, child);

    if (pos < builder->count && builder->edges[pos].child == child) {
        // 合并 label
        builder->edges[pos].label = sylvan_or(builder->edges[pos].label, label);
        sylvan_deref(label);
    } else {
        // 插入新 edge（需要移动后续元素）
        memmove(&builder->edges[pos + 1], &builder->edges[pos],
                (builder->count - pos) * sizeof(mtpndd_edge_record_t));
        builder->edges[pos] = (mtpndd_edge_record_t){child, label};
        builder->count++;
    }
    return true;
}
```

**权衡**：
- 优点：避免 finalize 时的排序
- 缺点：每次插入需要移动元素
- 适用场景：edge_num 小或插入已接近有序时

### 3.2 Edge Record 紧凑存储

**动机**：当前 `mtpndd_edge_record_t` 是 16 字节：

```c
typedef struct mtpndd_edge_record_s {
    mtpndd_t child;      // 8 bytes (uint64_t)
    mtpndd_bdd_t label;  // 8 bytes (uint64_t)
} mtpndd_edge_record_t;
```

如果节点索引 < 2^32 且 BDD 引用 < 2^32，可以压缩到 8 字节：

```c
typedef struct mtpndd_edge_record_compact_s {
    uint32_t child;  // 4 bytes
    uint32_t label;  // 4 bytes
} mtpndd_edge_record_compact_t;
```

**收益**：
- 内存减半
- Cache 利用率提升
- Hash 计算更快（更少数据）

**风险**：
- 需要确认 child 和 label 的实际范围
- 需要与 Sylvan 的 BDD 索引兼容

### 3.3 SIMD Hash 计算

**动机**：对于较大的 edge_num，可以使用 SIMD 并行计算 hash。

**示例（AVX2，处理 4 个 edge 并行）**：
```c
#ifdef __AVX2__
#include <immintrin.h>

static uint64_t mtpndd_node_struct_hash_simd(uint32_t field_id,
                                             const mtpndd_edge_record_t *edges,
                                             uint32_t edge_num) {
    // 处理 4 个 edge 为一组
    __m256i prime = _mm256_set1_epi64x(1099511628211ULL);
    __m256i h = _mm256_set1_epi64x(1469598103934665603ULL);

    uint32_t i = 0;
    for (; i + 4 <= edge_num; i += 4) {
        // 加载 4 个 child
        __m256i children = _mm256_loadu_si256((__m256i *)&edges[i]);
        h = _mm256_xor_si256(h, children);
        h = _mm256_mullo_epi64(h, prime);
        // ... 类似处理 label
    }

    // 合并 4 个 hash 值
    uint64_t results[4];
    _mm256_storeu_si256((__m256i *)results, h);
    uint64_t final = results[0] ^ results[1] ^ results[2] ^ results[3];

    // 处理剩余元素
    for (; i < edge_num; ++i) {
        final ^= (uint64_t)edges[i].child;
        final *= 1099511628211ULL;
        final ^= (uint64_t)edges[i].label;
        final *= 1099511628211ULL;
    }

    return mtpndd_hash_u64(final);
}
#endif
```

**适用场景**：edge_num > 8 时可能有收益。

### 3.4 Edge 比较优化

当前 edge 比较是逐个进行的：

```c
for (uint32_t i = 0; i < edge_num; ++i) {
    const mtpndd_edge_record_t stored = table->edge_pool.data[base + i];
    if (stored.child != edges[i].child || stored.label != edges[i].label) {
        return false;
    }
}
```

可以使用 `memcmp` 进行批量比较：

```c
return memcmp(&table->edge_pool.data[base], edges,
              edge_num * sizeof(mtpndd_edge_record_t)) == 0;
```

`memcmp` 通常有高度优化的实现（SIMD、word-at-a-time 等）。

---

## 4. 总结

| 优化 | feature/c | feature/serial | feature/index |
|------|-----------|----------------|---------------|
| Cached Hash | 适用（需原子操作） | 适用（已实现） | 不需要 |
| memset 初始化 | 适用 | 适用 | 不适用 |
| Sylvan-style Hash | 未实现 | 未实现 | 已内置 |
| Edge 预排序插入 | N/A | N/A | 可考虑 |
| Edge 紧凑存储 | N/A | N/A | 可考虑 |
| SIMD Hash | N/A | N/A | 可考虑 |
| memcmp 比较 | N/A | N/A | 可考虑 |

---

## 5. 已实现的优化

### 5.1 Operation Cache: 2-Way Set-Associative + Symmetric Hash

操作缓存用于存储 AND、OR、NOT 等操作的结果，避免重复计算。当前实现结合了两种正交的优化技术：

#### 5.1.1 Symmetric Hash（对称哈希）

对于交换律操作（如 AND、OR），`(a op b)` 和 `(b op a)` 结果相同。使用加法哈希消除操作数顺序的影响：

```c
static inline size_t mtpndd_op_cache_hash_binary_index(const mtpndd_op_cache_t *cache,
                                                        mtpndd_t lhs, mtpndd_t rhs) {
    // 加法具有交换性：hash(a,b) == hash(b,a)
    uint64_t hash = lhs + rhs;
    return (size_t)(hash & cache->mask);
}
```

**效果**：`AND(3, 5)` 和 `AND(5, 3)` 映射到同一个 bucket，无需存储两份。

#### 5.1.2 Key Canonicalization（键规范化）

在查找和存储时，还进行了显式的键规范化：

```c
// cache key canonicalization for commutative op
mtpndd_t lhs = a;
mtpndd_t rhs = b;
if (lhs > rhs) {
    mtpndd_t tmp = lhs;
    lhs = rhs;
    rhs = tmp;
}
```

这确保了 `(a, b)` 和 `(b, a)` 在缓存中使用完全相同的键 `(min, max)`。

#### 5.1.3 2-Way Set-Associative（2路组相联）

这是一种**碰撞处理策略**，与对称哈希是正交的概念：

```c
#define MTPNDD_CACHE_WAYS 2  // 每个 set 有 2 个槽位

// 内存分配：capacity * WAYS 个 entry
cache->entries = (mtpndd_op_cache_entry_t *)calloc(
    capacity * MTPNDD_CACHE_WAYS,
    sizeof(mtpndd_op_cache_entry_t));
```

**结构示意**：
```
Set 0: [Entry 0] [Entry 1]   ← 2 个槽位用于存储不同的键值对
Set 1: [Entry 2] [Entry 3]
Set 2: [Entry 4] [Entry 5]
...
```

**工作原理**：
- 当 `AND(3,5)` 和 `OR(2,6)` 碰巧哈希到同一个 set 时
- 2-way 设计允许两者同时存在于该 set 的两个槽位中
- 避免了一个操作结果驱逐另一个

#### 5.1.4 为什么需要 `capacity * WAYS` 内存？

| 概念 | 目的 | 内存影响 |
|------|------|----------|
| Symmetric Hash | 消除操作数顺序依赖 | 无额外内存 |
| 2-Way Associativity | 减少哈希碰撞驱逐 | 2× 内存 |

- **Symmetric Hash** 解决的是：`(a,b)` vs `(b,a)` 应该共享同一个缓存条目
- **2-Way** 解决的是：**不同的**操作对哈希到同一位置时的碰撞问题

这两种优化组合使用，既避免了重复存储交换等价的操作，又提高了缓存的有效容量（减少碰撞驱逐）。

#### 5.1.5 与 Java 版本的对比

Java 版本使用 `lhs + rhs` 作为哈希函数，避免存储 `(a,b)` 和 `(b,a)` 两份。当前 C 实现已经采用了相同的策略。

`capacity * MTPNDD_CACHE_WAYS` 的内存分配是为了 2-way 碰撞处理，不是为了操作数排序——后者已经通过对称哈希和键规范化解决了。

---

## 更新记录

- 2024-12-27: 添加 Operation Cache 2-way set-associative + symmetric hash 优化分析
- 2024-12-27: 初始版本，分析不同分支架构和优化适用性
