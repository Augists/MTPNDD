# MTPNDD 内存优化指南

> **快速导航**: 本指南提供 MTPNDD feature/index 分支内存优化的完整说明
>
> **更新日期**: 2026-01-12
>
> **状态**: 基线已测量，优化方向已确定

---

## 📋 目录

1. [快速开始](#快速开始)
2. [当前内存使用情况](#当前内存使用情况)
3. [优化路线图](#优化路线图)
4. [工具使用](#工具使用)
5. [相关文档](#相关文档)

---

## 🚀 快速开始

### 测量当前内存使用

```bash
# 构建项目
cmake --build build

# 测量 N=12 的内存使用（当前基线）
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1

# 查看结果
# 预期输出: PEAK MEMORY USAGE: ~772 MB
```

### 查看详细分析

```bash
# 阅读内存优化分析
less docs/memory_optimization_analysis.md

# 阅读执行摘要
less docs/memory_analysis_summary.md

# 查看工具使用说明
less tools/README.md
```

---

## 📊 当前内存使用情况

### 基线测量结果

**测试配置**:
- 测试用例: NQueens N=12, Worker=1
- 分支: feature/index (commit c9825e6)
- 构建时间: 5.89s
- 解数量: 14200 ✓

**内存统计**:
```
峰值 RSS (物理内存):  772.12 MB
峰值 VMS (虚拟内存):  6158.89 MB
VMS/RSS 比值:        ~8.0
```

### 内存组成估算

| 组件 | 估算大小 | 占比 |
|------|---------|------|
| Sylvan BDD 库 | 400-500 MB | 50-65% |
| MTPNDD Edge Pool | 120-150 MB | 15-19% |
| MTPNDD Node Table | 60-70 MB | 8-9% |
| MTPNDD Hash Table | 20-30 MB | 3-4% |
| 其他（栈、堆、缓存） | 100-120 MB | 13-15% |
| **总计** | **772 MB** | **100%** |

**主要内存消耗**:
- **Sylvan BDD**: 占据大部分内存（因为需要存储 BDD 标签）
- **Edge Pool**: 第二大消耗者（每条边 16 字节）
- **优化潜力**: Edge Pool 可减少 50%（如果采用紧凑存储）

---

## 🗺️ 优化路线图

### Phase 1: 快速优化（1-2 周）

**目标**: 低风险、快速见效

| 优化项 | 内存减少 | 性能提升 | 难度 | 状态 |
|--------|---------|---------|------|------|
| 自适应压缩阈值 | 5-10% | 2-3% | ⭐ | ⬜ 待实施 |
| Builder 池化 | 2-3% | 3-5% | ⭐⭐ | ⬜ 待实施 |

**预期累积收益**:
- 内存: 减少 50-100 MB (7-13%)
- 性能: 提升 5-8%
- 新峰值: ~670-720 MB

---

### Phase 2: 重点优化（3-4 周）

**目标**: 大幅减少内存占用

| 优化项 | 内存减少 | 性能提升 | 难度 | 前提条件 |
|--------|---------|---------|------|---------|
| Edge 紧凑存储 | 40-50% | 5-10% | ⭐⭐ | BDD 索引 < 2^32 |
| SIMD 边比较 | 0% | 3-5% | ⭐⭐ | - |

**预期累积收益**:
- 内存: 减少 300-400 MB (40-50%)
- 性能: 提升 13-23%
- 新峰值: ~380-470 MB

**⚠️ 关键验证**: 需先确认 Sylvan BDD 索引范围
```bash
# 检查 Sylvan BDD 的 mtbdd 定义
grep -rn "typedef.*mtbdd" sylvan/src/sylvan*.h
```

---

### Phase 3: 探索性优化（可选）

**目标**: 进一步压榨性能（仅在 Phase 2 不满足需求时）

| 优化项 | 内存减少 | 性能提升 | 难度 | 风险 |
|--------|---------|---------|------|------|
| SIMD Hash | 0% | 2-4% | ⭐⭐⭐ | 中 |
| 分代边池 | 10-15% | 5-8% | ⭐⭐⭐⭐⭐ | 高 |

---

## 📈 预期总收益

### 保守估计（Phase 1）

```
内存: 772 MB → 670-720 MB (减少 7-13%)
性能: 5.89s → 5.4-5.6s (提升 5-8%)
相对 v1.6: 累积提升 186-200%
```

### 乐观估计（Phase 1 + 2）

```
内存: 772 MB → 380-470 MB (减少 40-50%) ⚡ 接近减半！
性能: 5.89s → 4.7-5.3s (提升 13-23%)
相对 v1.6: 累积提升 230-270%
```

### 激进估计（Phase 1 + 2 + 3）

```
内存: 772 MB → 310-390 MB (减少 50-60%)
性能: 5.89s → 3.9-4.9s (提升 20-35%)
相对 v1.6: 累积提升 280-350%
```

---

## 🛠️ 工具使用

### 1. 单次内存测量

**测量 N=12 的内存使用**:
```bash
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1
```

**输出示例**:
```
========================================
Memory Statistics (/proc monitoring)
========================================
Samples collected: 1157
Peak RSS (Physical Memory): 790648 KB (772.12 MB)
Peak VMS (Virtual Memory):  6306704 KB (6158.89 MB)

Exit code: 0

==========================================
PEAK MEMORY USAGE: 772.12 MB
==========================================
```

---

### 2. 批量对比测试

**测试多个 N 值**:
```bash
./tools/memory_compare.sh
```

**输出**:
- 每个 N 值的详细报告
- 汇总文件: `/tmp/memory_results_<timestamp>/summary.txt`

---

### 3. 优化前后对比

**模板脚本**:
```bash
#!/bin/bash
# 1. 记录基线
git checkout feature/index
cmake --build build
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1 \
    | tee baseline.txt

# 2. 实施优化
git checkout -b feature/opt-<name>
# ... 修改代码 ...

# 3. 测试优化后
cmake --build build
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1 \
    | tee optimized.txt

# 4. 对比结果
echo "=== Baseline ==="
grep "PEAK MEMORY" baseline.txt
echo "=== Optimized ==="
grep "PEAK MEMORY" optimized.txt

# 5. 计算改进
baseline=$(grep "PEAK MEMORY" baseline.txt | awk '{print $4}')
optimized=$(grep "PEAK MEMORY" optimized.txt | awk '{print $4}')
echo "Baseline:  ${baseline} MB"
echo "Optimized: ${optimized} MB"
echo -n "Improvement: "
awk "BEGIN {printf \"%.1f%%\n\", (($baseline - $optimized) / $baseline) * 100}"
```

---

### 4. 详细堆分析（Valgrind Massif）

**适用场景**: 需要详细的堆内存分析时（较慢）

```bash
# 运行 Massif（用于小规模测试）
./tools/measure_memory.sh massif ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 10 1

# 查看详细报告
MASSIF_OUT=$(ls -t /tmp/massif.out.* | head -1)
ms_print "$MASSIF_OUT" | less

# 查看峰值快照
ms_print "$MASSIF_OUT" | grep -A 20 "peak"
```

---

## 📚 相关文档

### 核心文档

| 文档 | 描述 | 用途 |
|------|------|------|
| `memory_analysis_summary.md` | 执行摘要和实施计划 | 快速了解优化方向 ⭐ 推荐先看 |
| `memory_optimization_analysis.md` | 详细技术分析 | 深入理解优化原理 |
| `temp_refs_results.md` | temp_refs 优化记录 | 了解已完成的优化 |
| `tools/README.md` | 工具使用说明 | 学习如何使用测量工具 |

### 架构与设计

| 文档 | 描述 |
|------|------|
| `mtpndd_architecture_zh.png` | 模块架构图 |
| `mtpndd_memory_design_zh.png` | 内存布局图 (feature/index) |
| `mtpndd_mk_flow_zh.png` | 节点唯一化流程 |

---

## 🔧 实施 Checklist

### Phase 1 准备工作

- [x] 创建内存测量工具
- [x] 运行基线测试（772 MB）
- [ ] 验证 Sylvan BDD 索引范围（确定 Phase 2 可行性）
- [ ] 记录当前的节点数和边数统计

### Phase 1 实施

- [ ] 实施自适应压缩阈值（1 天）
- [ ] 实施 Builder 池化（2-3 天）
- [ ] 全面测试 Phase 1 累积效果
- [ ] 决策：Phase 1 效果是否满足需求

### Phase 2 实施（如果需要）

- [ ] 验证 BDD 索引 < 2^32（前置条件）
- [ ] 实施 Edge 紧凑存储（5-7 天）
- [ ] 实施 SIMD 边比较（3-5 天）
- [ ] 全面测试 Phase 2 累积效果
- [ ] 决策：是否需要 Phase 3

### Phase 3 评估（如果需要）

- [ ] 评估 SIMD Hash 的可行性
- [ ] 评估分代边池的必要性
- [ ] 风险评估和成本收益分析

---

## ⚠️ 注意事项

### 正确性优先

**每次优化后必须验证**:
```bash
# 基本正确性测试
./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 8 1   # 预期 92
./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 9 1   # 预期 352
./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 10 1  # 预期 724
./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1  # 预期 14200

# 内存泄漏检测
valgrind --leak-check=full ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 10 1
```

### 性能回归检测

**每次优化后测试性能**:
```bash
# 3 次取平均
for i in 1 2 3; do
    ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1 | grep "baseline_set_sizes"
done
```

**决策标准**:
- 内存减少 < 3%: 考虑放弃该优化
- 性能退化 > 1%: 分析原因，可能需要回退
- 正确性问题: 立即回退，重新评估

### 风险管理

**高风险优化**:
- Edge 紧凑存储: 需验证 BDD 索引范围
- 分代边池: 架构变更大，实施复杂

**缓解措施**:
- 每个优化独立分支
- 充分的单元测试
- 保留回退路径

---

## 📞 获取帮助

### 工具问题

参见 `tools/README.md` 的故障排除章节

### 优化问题

参见 `docs/memory_optimization_analysis.md` 的风险评估章节

---

## 📝 更新日志

### 2026-01-12

- ✅ 创建内存测量工具
- ✅ 完成基线测量（772 MB）
- ✅ 识别 6 个优化方向
- ✅ 制定 3 阶段优化路线图
- ✅ 编写完整文档和使用指南

### 待办事项

- ⬜ 验证 Sylvan BDD 索引范围
- ⬜ 实施 Phase 1 优化
- ⬜ 测试并记录 Phase 1 结果

---

**维护者**: Claude Code Analysis
**版本**: 1.0
**最后更新**: 2026-01-12
