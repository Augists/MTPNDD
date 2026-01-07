# MTPNDD 性能分析报告：C实现 vs Java版本

## 1. 结论与当前目标（最新）
- **核心结论**：Sylvan/Lace 版本差异是当前性能差距的主因。基于对比，**bundled Lace 的版本表现最好**。
- **新增结论**：移除 `gc_protect` 集合并改为临时节点 `ref/deref` 后，NQueens 性能显著提升。
- **当前目标**：
  1) 对比三套 NDD 实现的关键步骤耗时（MTPNDD / Java-JDD / Java-JSylvan）。
  2) 重点分析 **节点表查找** 与 **BDD 调用** 的差异来源，验证改动是否有效。
- **约束**：目前不引入“BDD 边复用”，保持与 `reference/NDD` main 分支一致的行为。

## 2. 关键原因（版本级差异）
1. **旧版 Sylvan 使用 `CALL` + `LACE_ME`**：在旧版 Lace 中，`LACE_ME` 允许当前线程作为 worker 执行，`CALL` 路径接近函数调用。
2. **新版 Sylvan 使用 `RUN`**：`RUN` 在外部线程路径上会触发 `lace_run_task` 的同步开销（resume/suspend、锁、信号量），在 NQueens 高频 BDD 调用下成本显著累积。
3. **JSylvan 新版适配移除 `LACE_ME`**：在新 Lace 语义下无法在 Java 线程创建 worker 上下文，导致所有操作走慢路径。

## 3. MTPNDD 当前基线（NQueens N=12）
- 命令：`./sylvan/build/src/sylvan/mtpndd/mtpndd_nqueens_benchmark 12`（`MTPNDD_ENABLE_RECORDING=ON`）
- 结果：solutions=14200，总耗时 24.761s
- AND 时间分解：
  - and/or/not 总时间：24.512 / 0.008 / 0.001 s
  - and 分解：fast=0.122，cache=2.122，build_edges=0.440，diff=0.614，mk=10.145 s
  - same-field 细分：outer=1.000，inner=0.800，label=0.685，bdd_op=6.305，add_edge=1.971 s
- MK 内部分解：
  - mk call/cache/other：7.091 / 7.839 / 0.006 s
  - mk 细分：hash=0.698，lookup=3.657，fast=0.014，reuse=0.865，ref=0.123，gc=0.050，alloc_node=0.103，alloc_entry=0.115 s
  - mk scan/link/other：scan=0.111，link=0.582，other=0.238 s
- 节点表查找细分：
  - nodetable hash/bucket/compare = 0.163 / 2.690 / 0.561 s
  - lookup hits/misses = 7,653,713 / 1,848,510，avg_steps=1.77，max_steps=12

> 说明：本次统计不包含 `satcount` 的转换成本。

## 3.1 MTPNDD 关闭 recording 基线（NQueens N=12）
- 命令：`./sylvan/build/src/sylvan/mtpndd/mtpndd_nqueens_benchmark 12`（`MTPNDD_ENABLE_RECORDING=OFF`）
- 结果：solutions=14200，总耗时 16.408s

## 4. 三套实现可对比步骤（N=12）
| 步骤 | MTPNDD (C) | NDD-test (JDD) | NDD_sylvan (JSylvan + bundled lace) |
|------|-----------|----------------|-------------------------------------|
| Total | 24.761 | 14.250 | 19.324 |
| and / or / not | 24.512 / 0.008 / 0.001 | 14.211 / 0.005 / 0.000 | 19.205 / 0.034 / 0.000 |
| and same-field bdd / add | 6.305 / 1.971 | 1.114 / 1.183 | 2.143 / 3.418 |
| mk total | 6.556 | 6.107 | 7.324 |
| mk lookup | 3.657 | 3.460 | 3.751 |
| mk ref children | 0.123 | 0.468 | 0.537 |
| mk gc | 0.050 | 0.000 | 0.000 |
| mk create / alloc | 0.218 | 0.914 | 1.190 |
| and cache hit/miss | 1,037,486 / 9,980,602 | 437,052 / 11,531,854 | 421,533 / 11,568,418 |

**Java 无法细分的项**：
- nodetable bucket scan / per-bucket step 数
- mk 内部 hash/rehash/link 的细拆
- and same-field 的 outer/inner/label_load

## 5. 节点表查找分析（当前关注）
**现象**：MTPNDD 的 nodetable bucket + compare 合计约 3.93s，显著高于 Java 的 edgeMap hash/equals（约 1.25–1.37s）。

**原因猜测**：
1) MTPNDD 需要遍历 nodetable bucket 链表并做逐边比较（含原子读 label）；
2) Java HashMap 能更早过滤不匹配边集合（hash/equals 由 JVM 优化）。

**已完成实验**：move-to-front 与增大 nodetable bucket 数量均无收益（见附录）。

**下一步方向**：
- 降低 bucket 扫描成本：减少 bucket 链长度或减少 compare 调用次数；
- 优化 edge compare：减少原子 label 读或引入更轻量的比较路径。

## 6. 性能剖析建议
```bash
perf record ./mtpndd_nqueens_test 10
perf report
# 或
valgrind --tool=callgrind ./mtpndd_nqueens_test 10
kcachegrind callgrind.out.*
```

## 6.1 当前分支待尝试优化点
- nodetable bucket index 的轻量混合（如 `cached_hash ^ (cached_hash >> 16)` 后再取 mask）。
- nodetable compare 进一步早筛：在 bucket 扫描前先比 `entry_edges` 指针或更多元信息。
- 比较路径减少原子读：探索 label 读取的替代方案或批量读取。

## 7. 附录：次要观察项与已完成实验
### 7.0 已完成的方案与基线
- 更新 Sylvan 到最新版并建立新基线。
- 在 MTPNDD 使用的 Sylvan 中集成 bundled Lace，并完成兼容调整。
- 让 MTPNDD 运行时兼容 bundled Lace。
- 移除 `gc_protect` 集合，改为临时节点 `ref/deref` 保护。

### 7.1 nodetable 实验记录
- 说明：本节多数实验基于 25.481s 旧基线（bitmask 优化前）；当前基线为 24.761s。
- move-to-front（bucket 内命中项移到头部）：
  - recording=ON：25.481s → 27.117s（变慢）
  - recording=OFF：16.685s → 17.141s（变慢）
  - 结论：无收益，已撤回
- 额外 fingerprint（cached_hash 之外再加 commutative fingerprint）：
  - recording=ON：25.481s → 25.552s（略慢），bucket/compare 从 2.913/1.017 降至 2.768/0.965
  - recording=OFF：16.685s → 17.489s（变慢）
  - 结论：bucket/compare 有小幅下降，但总耗时上升，已撤回
- 增量维护 cached_hash（在 add_edge 中更新，避免 mk 全量 hash）：
  - recording=ON：25.481s → 25.810s（变慢），mk_hash 从 0.690 降至 0.286
  - recording=OFF：16.685s → 18.093s（变慢）
  - 结论：hash 计算成本下降但总耗时上升，已撤回
- 增大 nodetable bucket（`nodetable_bucket_count=1<<21`）：
  - recording=ON：25.481s → 26.225s（变慢），bucket/compare 从 2.913/1.017 降至 2.755/0.786
  - recording=OFF：16.685s → 16.846s（变慢）
  - 结论：bucket/compare 有下降，但总耗时上升
- bucket 预筛选（先比 cached_hash + edge_count 再调用边比较）：
  - recording=ON：25.481s → 24.858s（变快），bucket/compare 从 2.913/1.017 降至 2.729/0.634
  - recording=OFF：16.685s → 16.802s（略慢）
  - 结论：细粒度耗时明显下降，但 release 版本收益不稳定
- bucket index 使用位运算（`cached_hash & (bucket_count - 1)`，仅在 bucket_count 为 2 的幂）：
  - recording=ON：25.481s → 24.705s（变快），hash/bucket/compare=0.163/2.652/0.556
  - recording=OFF：16.685s → 16.408s（变快）
  - 结论：轻微收益，保留

### 7.2 op cache hash 实验记录（N=12，关闭 recording）
- 基线（原始 hash 函数）：17.366s
- 简化 hash mix（更少混合）：17.521s（变慢）
- 内联原始 hash mix（移除 helper 函数）：16.907s（变快）
结论：保留内联实现，简化 mix 无收益。

### 7.3 桶数对比实验（N=12，关闭 recording）
- `edge_bucket_count=0`：16.734s / 16.685s
- `edge_bucket_count=16`：17.856s / 17.868s
结论：扩桶无收益，反而变慢约 ~1.1s。

### 7.4 操作缓存
- 命中率低但可接受（约 12.7%），当前不作为优化目标。

### 7.5 碰撞与 rehash
- edge collision 约 34.7%，nodetable collision 约 1273 次；本轮不作为主线目标。
- 后续如需验证：调整 `edge_bucket_count` 或 rehash 阈值后对比 `edge_collisions` 与 `and_*` 时间。

### 7.6 后置优化建议（不影响当前结论）
- **缓存行对齐**：关键数据结构按缓存行对齐，减少 false sharing。
- **分支预测**：热点路径添加 `__builtin_expect` 提示，降低分支误判。
- **减少 BDD 边引用**：批量 ref/deref 或复用局部 label，减少引用计数操作。
- **优化边合并**：小边集合用线性扫描合并，减少排序/去重的固定成本。
- **优化 mk 新节点路径**：减少重复 hash/比较与临时分配，必要时引入更轻量的“命中快速返回”分支。

### 7.7 相关代码位置
| 模块 | 文件 | 关键函数 |
|------|------|---------|
| 操作缓存 | `sylvan/src/sylvan/mtpndd/mtpndd_operation_cache.c` | `lookup_binary`, `store_binary` |
| AND 操作 | `sylvan/src/sylvan/mtpndd/mtpndd_node.c` | `mtpndd_and_rec` |
| OR/NOT | `sylvan/src/sylvan/mtpndd/mtpndd_node.c` | `mtpndd_or_rec`, `mtpndd_not_rec` |
| 节点创建 | `sylvan/src/sylvan/mtpndd/mtpndd_nodetable.c` | `mtpndd_mk` |
| 运行入口 | `sylvan/src/sylvan/mtpndd/mtpndd_common.c` | `mtpndd_init`, `mtpndd_quit` |
| NQueens 测试 | `sylvan/src/sylvan/mtpndd/test/nqueens.c` | `run_case` |

### 7.8 旧版本基线（移除 gc_protect 之前）
- N=12 总耗时 61.848s（solutions=14200）。
- and mk call/gc/cache/other：6.304 / 36.697 / 0.569 / 0.883 s
- gc_protect 内部：hash=0.369，lookup=34.386，alloc=0.355，record=0.153，link=0.235 s
- gc_protect 链表统计：hits/misses=1,623,576/7,878,905，avg_steps=238.72，max_steps=1502
