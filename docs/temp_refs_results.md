# temp_refs 优化结果 (feature/index)

## 优化内容

将 `gc_protect` (基于哈希表) 替换为 `temp_refs` (基于动态数组)，用于在递归操作中保护中间结果。

### 核心原理

**gc_protect 问题:**
- 使用哈希表存储受保护的节点
- 哈希计算 + 线性探测开销
- 负载因子 > 0.7 时 rehash
- 随机内存访问，缓存不友好

**temp_refs 优势:**
- 简单动态数组，顺序内存访问
- Push 操作 O(1) 摊销
- 无哈希计算，无冲突处理
- 批量释放，缓存友好

## 实现细节

### 数据结构
```c
typedef struct {
    mtpndd_t *items;      // 动态数组
    size_t count;         // 当前元素数
    size_t capacity;      // 容量
} mtpndd_temp_ref_list_t;
```

### 核心函数
```c
// 初始化
void mtpndd_temp_refs_init(list);

// 添加节点（使用 mtpndd_ref）
bool mtpndd_temp_refs_push(list, node);

// 释放所有节点（使用 mtpndd_deref）
void mtpndd_temp_refs_release(list);
```

### 修改的函数
- `mtpndd_and_rec` - 添加 temp_refs 参数
- `mtpndd_or_rec` - 添加 temp_refs 参数
- `mtpndd_not_rec` - 添加 temp_refs 参数
- `mtpndd_exist_rec` - 添加 temp_refs 参数

所有递归调用返回的中间结果立即 push 到 temp_refs。

### 公开包装函数
```c
mtpndd_t mtpndd_and(mtpndd_t a, mtpndd_t b) {
    mtpndd_temp_ref_list_t temp_refs;
    mtpndd_temp_refs_init(&temp_refs);
    mtpndd_t result = mtpndd_and_rec(a, b, &temp_refs);
    mtpndd_temp_refs_release(&temp_refs);
    return result;
}
```

## 关键 Bug 修复

**错误实现:** 使用 `mtpndd_protect/unprotect`
- `mtpndd_protect` 设置 `ref_count = MTPNDD_REFCOUNT_PROTECTED`
- `mtpndd_unprotect` 设置 `ref_count = 0`
- **问题:** 覆盖了已有的 ref_count，导致节点被错误回收

**正确实现:** 使用 `mtpndd_ref/deref`
- `mtpndd_ref` 递增 ref_count（除非已 PROTECTED）
- `mtpndd_deref` 递减 ref_count（除非已 PROTECTED）
- **正确:** 保持引用计数的完整性

## 性能测试结果

### 测试配置
- 测试用例: NQueens N=12, Worker=1
- 测试次数: 3 次取平均值
- 分支: feature/index
- 对比基线: 7.05s (Sylvan v1.9.1 + Worker=1 优化后)

### 测试结果

| Run | Build Time (s) | Total Time (s) |
|-----|----------------|----------------|
| 1   | 5.930          | 6.401          |
| 2   | 5.897          | 6.363          |
| 3   | 5.869          | 6.336          |
| **Avg** | **5.899**  | **6.367**      |

### 性能提升

- **基线:** 7.05s
- **优化后:** 5.899s
- **提升:** 16.3% (减少 1.151s)
- **预期:** 5-8% (基于 feature/c 的测试)
- **实际:** 超出预期 2 倍！

## 分析

temp_refs 优化在 feature/index 上表现优异，原因可能包括:

1. **数组池化架构的协同效应:**
   - feature/index 的数组池化内存管理
   - temp_refs 的顺序内存访问
   - 两者共同改善缓存局部性

2. **减少内存管理开销:**
   - 避免 gc_protect 的哈希表开销
   - 减少内存分配/释放次数
   - 降低 GC 压力

3. **编译器优化:**
   - 简单数组操作更易优化
   - 内联机会更多

## 正确性验证

- N=8: 92 solutions ✓
- N=9: 352 solutions ✓
- N=10: 724 solutions ✓
- N=12: 14200 solutions ✓

所有测试用例通过。

## 结论

temp_refs 优化在 feature/index 分支上实现了 **16.3%** 的性能提升，超出预期效果。优化保持了代码的正确性，并且与 feature/index 的数组池化架构形成良好的协同效应。

建议合并到主分支。
