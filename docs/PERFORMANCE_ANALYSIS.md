# MTPNDD 性能分析报告：串行优化版本（feature/serial）

## 1. 当前性能基线（最新更新：2026-01-10）

### 1.1 测试结果（NQueens N=12, Worker=1）
- **当前性能**：**11.785s**
- 解数量：14200
- 测试命令：`./sylvan/build/src/sylvan/mtpndd/mtpndd_nqueens_benchmark 12 1`

### 1.2 优化历程

| 阶段 | 时间(s) | 优化内容 | 提升幅度 |
|------|---------|---------|---------|
| 初始版本 | 61.8s | 使用 gc_protect | baseline |
| 移除 gc_protect | 28.1s | 改用 temp_refs | 54.6% |
| Cache size 优化 | **11.785s** | 16384 → 524288 (32x) | 58.1% |
| **总体提升** | - | - | **80.9%** |

### 1.3 核心优化

1. **移除 gc_protect（2026-01）**
   - 问题：gc_protect 哈希表查找成为瓶颈（平均步数 238.72）
   - 方案：改用局部 temp_ref_list 临时持有节点引用
   - 收益：MK 时间从 46.4s 降至 11.4s（提升 75.4%）

2. **Cache size 优化（2026-01-10）**
   - 问题：operation cache 太小（16384），命中率低
   - 方案：扩大到 524288（32x），提升 cache 命中率
   - 收益：性能从 28.1s 提升到 11.785s（58.1%）

## 2. 技术细节

### 2.1 Sylvan/Lace 版本选择
- **结论**：bundled Lace 版本性能最好
- **原因**：
  - 旧版 Sylvan 使用 `CALL` + `LACE_ME`，外部线程可直接执行
  - 新版 Sylvan 使用 `RUN`，外部线程需要同步开销
  - bundled Lace 优化了任务调度路径

### 2.2 串行化收益分析
- **LACE 同步开销**：在高频调用场景下（NQueens N=12），LACE 的 resume/suspend、锁、信号量开销显著
- **节点表查找优化**：移除并行后，查找路径简化，平均步数从 238.72 降至 1.77
- **内存管理简化**：temp_refs 替代 gc_protect，避免哈希表查找

### 2.3 Cache size 优化原理
- **之前**：16384 entries，约占 128KB 内存
- **之后**：524288 entries，约占 4MB 内存
- **收益**：cache 命中率从 70-80% 提升到 90%+，大幅减少重复计算
- **代价**：内存占用增加 ~4MB（可接受）

## 3. 性能对比

### 3.1 与 Java 实现对比（NQueens N=12）
| 实现 | 时间 | 说明 |
|------|------|------|
| MTPNDD (C, serial) | **11.785s** | 当前版本 |
| Java NDD (JDD) | ~14s | 参考实现 |
| Java JSylvan | ~19s | JSylvan 适配版 |

**MTPNDD 优势**：
- 比 Java JDD 快约 20%
- 比 Java JSylvan 快约 40%

### 3.2 优化前后对比（N=12）
| 指标 | gc_protect 版本 | temp_refs 版本 | Cache优化后 | 提升 |
|------|----------------|---------------|------------|------|
| 总时间 | 61.8s | 28.1s | **11.785s** | **80.9%** |
| MK 时间 | 46.4s | 11.4s | ~4s | **91.4%** |
| 节点表平均步数 | 238.72 | 1.77 | 1.77 | **99.3%** |

## 4. 下一步优化方向

### 4.1 进一步优化空间
1. **BDD 调用优化**：Sylvan BDD 调用仍占约 30% 时间，可研究缓存或批量处理
2. **节点表查找**：虽然平均步数已很低(1.77)，但仍有优化空间（目标 <1.5）
3. **内存分配**：研究 memory pool 的分配策略，减少碎片

### 4.2 不建议的方向
- **重新引入并行**：在当前实现下，LACE 同步开销大于收益（见 feature/c 分支实验）
- **过度优化细节**：当前性能已达到可接受水平，应优先保证代码可维护性

## 5. 约束与限制

- **不引入 BDD 边复用**：保持与 `reference/NDD` main 分支一致的行为
- **串行实现**：当前版本为串行实现，不利用多核（但单核性能最优）
- **Worker=1**：虽然支持 worker 参数，但当前实现在 Worker=1 时性能最优

## 6. 测试与验证

### 6.1 运行测试
```bash
# 编译
cd sylvan
cmake -B build -DMTPNDD_ENABLE_RECORDING=ON
cmake --build build

# 运行基准测试（Worker=1，推荐）
./build/src/sylvan/mtpndd/mtpndd_nqueens_benchmark 12 1

# 运行测试（验证正确性）
./build/src/sylvan/mtpndd/mtpndd_nqueens_test 8
```

### 6.2 性能分析
```bash
# 使用 perf 分析
perf record ./build/src/sylvan/mtpndd/mtpndd_nqueens_benchmark 12 1
perf report

# 使用 valgrind callgrind
valgrind --tool=callgrind ./build/src/sylvan/mtpndd/mtpndd_nqueens_benchmark 12 1
kcachegrind callgrind.out.*
```

## 7. 参考文档

- `docs/feature_serial_summary.md`：feature/serial 分支的详细总结
- `sylvan/docs/PARALLELIZATION_ATTEMPTS.md`：并行化尝试（feature/c 分支）
- Sylvan 文档：https://github.com/trolando/sylvan
