# MTPNDD 性能分析报告：C实现 vs Java版本

## 1. 结论与当前目标（最新）
- **核心结论**：Sylvan/Lace 版本差异是当前性能差距的主因。基于对比，**bundled Lace 的版本表现最好**。
- **当前目标**：
  1) 移除 gc_protect 集合，改为对临时节点直接 ref/deref。
- **约束**：目前不引入“BDD 边复用”，保持与 `reference/NDD` main 分支一致的行为。

## 2. 关键原因（版本级差异）
1. **旧版 Sylvan 使用 `CALL` + `LACE_ME`**：在旧版 Lace 中，`LACE_ME` 允许当前线程作为 worker 执行，`CALL` 路径接近函数调用。
2. **新版 Sylvan 使用 `RUN`**：`RUN` 在外部线程路径上会触发 `lace_run_task` 的同步开销（resume/suspend、锁、信号量），在 NQueens 高频 BDD 调用下成本显著累积。
3. **JSylvan 新版适配移除 `LACE_ME`**：在新 Lace 语义下无法在 Java 线程创建 worker 上下文，导致所有操作走慢路径。

## 3. MTPNDD 当前基线（NQueens N=12）
- 命令：`./sylvan/build/src/sylvan/mtpndd/mtpndd_nqueens_benchmark 12`（`MTPNDD_ENABLE_RECORDING=ON`）
- 结果：solutions=14200，总耗时 61.848s
- AND 时间分解：
  - and/or/not 总时间：61.484 / 0.009 / 0.001 s
  - and 分解：fast=0.117，cache=2.424，build_edges=0.397，diff=0.650，mk=46.442 s
  - same-field 细分：outer=1.051，inner=0.850，label=0.716，bdd_op=6.608，add_edge=1.927 s
- MK 内部分解：
  - mk call/gc/cache/other：6.304 / 36.697 / 0.569 / 0.883 s
  - gc_protect 内部：hash=0.369，lookup=34.386，alloc=0.355，record=0.153，link=0.235 s
  - gc_protect 链表统计：hits/misses=1,623,576/7,878,905，avg_steps=238.72，max_steps=1502

> 说明：本次统计不包含 `satcount` 的转换成本。

## 4. 性能剖析建议
```bash
perf record ./mtpndd_nqueens_test 10
perf report
# 或
valgrind --tool=callgrind ./mtpndd_nqueens_test 10
kcachegrind callgrind.out.*
```

## 5. 附录：次要观察项与后置优化
### 5.0 已完成的方案与基线
- 更新 Sylvan 到最新版并建立新基线。
- 在 MTPNDD 使用的 Sylvan 中集成 bundled Lace，并完成兼容调整。
- 让 MTPNDD 运行时兼容 bundled Lace。

### 5.1 操作缓存
- 命中率低但可接受（约 12.7%），当前不作为优化目标。

### 5.2 碰撞与 rehash
- edge collision 约 34.7%，nodetable collision 约 1273 次；本轮不作为主线目标。
- 后续如需验证：调整 `edge_bucket_count` 或 rehash 阈值后对比 `edge_collisions` 与 `and_*` 时间。

### 5.3 后置优化建议（不影响当前结论）
- **缓存行对齐**：关键数据结构按缓存行对齐，减少 false sharing。
- **分支预测**：热点路径添加 `__builtin_expect` 提示，降低分支误判。
- **减少 BDD 边引用**：批量 ref/deref 或复用局部 label，减少引用计数操作。
- **移除 gc_protect 集合**：对“临时节点”改为直接 `ref/deref` 管理生命周期，避免每次操作清空保护集的固定成本。
- **优化边合并**：小边集合用线性扫描合并，减少排序/去重的固定成本。
- **优化 mk 新节点路径**：减少重复 hash/比较与临时分配，必要时引入更轻量的“命中快速返回”分支。

### 5.4 相关代码位置
| 模块 | 文件 | 关键函数 |
|------|------|---------|
| 操作缓存 | `sylvan/src/sylvan/mtpndd/mtpndd_operation_cache.c` | `lookup_binary`, `store_binary` |
| AND 操作 | `sylvan/src/sylvan/mtpndd/mtpndd_node.c` | `mtpndd_and_rec` |
| OR/NOT | `sylvan/src/sylvan/mtpndd/mtpndd_node.c` | `mtpndd_or_rec`, `mtpndd_not_rec` |
| 节点创建 | `sylvan/src/sylvan/mtpndd/mtpndd_nodetable.c` | `mtpndd_mk` |
| GC Protect | `sylvan/src/sylvan/mtpndd/mtpndd_common.c` | `mtpndd_gc_protect_*` |
| NQueens 测试 | `sylvan/src/sylvan/mtpndd/test/nqueens.c` | `run_case` |
