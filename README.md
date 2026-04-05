# MTPNDD: A library for Multi-Terminal Network Decision Diagram in **Parallel**

使用 C 语言重构 NDD，并实现并行和多终端节点需求

## Architecture

![architecture](docs/mtpndd_architecture_en.png)

## Build and Run

<details>
<summary>Sphinx for doc generating</summary>

https://www.sphinx-doc.org/en/master/usage/installation.html

for macOS,

```bash
brew install sphinx-doc
```
</details>

### MTPNDD with Sylvan and Lace

```bash
cd sylvan
cmake -B build -DMTPNDD_LOG_LEVEL=2
cmake --build build     # 生成 libsylvan.a、libmtpndd.a 等
./build/src/sylvan/mtpndd/mtpndd_nqueens_test 8
```

### JNI

```bash
cd jni
cmake -B build -DMTPNDD_LOG_LEVEL=2
cmake --build build     # 得到 build/libmtpnddjni.so

mvn -DskipTests package # 产出 target/mtpndd-java-0.1.0-SNAPSHOT.jar
mvn -Dorg.ants.mtpndd.library.path="$PWD/build/libmtpnddjni.so" test
java -Dorg.ants.mtpndd.library.path=$PWD/build/libmtpnddjni.so -cp target/mtpndd-java-0.1.0-SNAPSHOT.jar:target/test-classes org.ants.mtpndd.NQueensMTPNDD 8 onehot
```

### Configuration

可通过 `mtpndd_pal_config_t` 提供的可选字段调整内部哈希表的桶数量：

```c
mtpndd_pal_config_t config = {
    .n_workers = 0,
    .lace_dqsize = 1 << 18,
    .bdd_nodetable_size = 1 << 16,
    .mtpndd_nodetable_size = 1 << 14,
    .op_cache_size = 1 << 12,
    .edge_bucket_count = 8,            // 必须 <= 64（单个 64-bit bitset），默认为 8
    .nodetable_bucket_count = 1 << 12, // 默认为 1024 或 65537 (LARGE_NODETABLE)
    .node_slab_capacity = 1024,        // 节点/edge/nodetable/edge-map 含义接近，统一容量，便于调优
    .edge_entry_slab_capacity = 1024,
    .nodetable_entry_slab_capacity = 1024,
    .edge_map_slab_capacity = 1024,
    .gc_bucket_count = 64,        // 默认为 1024 或 65537
    .gc_protect_entry_slab_capacity = 256 // 默认 256，可按需要增减
};
mtpndd_init(&config);
```

未配置时会自动使用默认值，适合小规模问题。对于节点/边数量巨大的场景，可以按需增大桶数量或调节 slab 大小以降低哈希冲突和频繁分配的开销。

`gc_protect_entry_slab_capacity` 可较小（例如 256/384/512/640），因为其占用和热点访问都远少于核心池。

节点、边映射以及节点表 entry 均通过 slab 内存池管理，在初始化阶段会按上述容量参数批量预留对象并在回收时复用，避免频繁的 `malloc/free` 带来的锁竞争开销。

## Visualization

调用 `mtpndd_print_dot(root)` 或 `mtpndd_fprint_dot(file, root)` 可以把当前节点为根的 NDD 导出为 DOT 描述。例如：

```c
FILE *fp = fopen("graph.dot", "w");
mtpndd_fprint_dot(fp, my_root);
fclose(fp);
// dot -Tpng graph.dot -o graph.png
```

默认的 `mtpndd_print_dot` 会直接输出到标准输出，便于通过管道交给 Graphviz 处理。

## Benchmark

N-Queens 是 MTPNDD 的主要性能验收场景。以下数据在 `MTPNDD_LOG_LEVEL=0`（release 编译）下测得，
时间包含 `mtpndd_init`、字段声明、全部构建与 `satcount`。

**串行版**（`nqueens_benchmark`，仅操作内并行）与**并行版**（`nqueens_parallel_benchmark`，
操作间并行 + 操作内并行）在相同内存配置（`nodetable_bucket_count = bdd_size/4`，`edge_bucket_count = 16`）下对比。

### 串行版（nqueens_benchmark）

| N  | w=1 (s) | w=2 (s) | w=3 (s) | w=4 (s) | w=5 (s) | w=6 (s) | 解数   |
|----|---------|---------|---------|---------|---------|---------|--------|
| 7  | 0.016   | 0.014   | 0.012   | 0.013   | 0.012   | 0.013   | 40     |
| 8  | 0.038   | 0.030   | 0.024   | 0.023   | 0.023   | 0.025   | 92     |
| 9  | 0.123   | 0.095   | 0.067   | 0.061   | 0.055   | 0.051   | 352    |
| 10 | 0.489   | 0.350   | 0.304   | 0.213   | 0.177   | 0.154   | 724    |
| 11 | 2.329   | 1.611   | 0.984   | 0.905   | 0.752   | 0.629   | 2680   |
| 12 | 12.377  | 8.552   | 5.078   | 4.641   | 3.756   | 3.114   | 14200  |
| 13 | 75.547  | 48.917  | 30.500  | 26.712  | 21.290  | 17.617  | 73712  |

### 并行版（nqueens_parallel_benchmark，Phase 1+2 操作间并行）

| N  | w=1 (s) | w=2 (s) | w=3 (s) | w=4 (s) | w=5 (s) | w=6 (s) | 解数   |
|----|---------|---------|---------|---------|---------|---------|--------|
| 7  | 0.017   | 0.012   | 0.014   | 0.010   | 0.010   | 0.010   | 40     |
| 8  | 0.037   | 0.027   | 0.020   | 0.019   | 0.017   | 0.025   | 92     |
| 9  | 0.121   | 0.089   | 0.059   | 0.058   | 0.045   | 0.040   | 352    |
| 10 | 0.483   | 0.337   | 0.217   | 0.202   | 0.165   | 0.140   | 724    |
| 11 | 2.271   | 1.560   | 0.953   | 0.888   | 0.796   | 0.590   | 2680   |
| 12 | 12.130  | 8.221   | 4.978   | 4.623   | 3.680   | 3.077   | 14200  |
| 13 | 70.742  | 47.927  | 28.935  | 26.656  | 21.630  | 17.536  | 73712  |

### 操作间并行加速比（parallel / serial，w=6）

| N  | 串行 w=6 (s) | 并行 w=6 (s) | 加速比 |
|----|------------|------------|--------|
| 7  | 0.013      | 0.010      | **1.30x** |
| 8  | 0.025      | 0.025      | 1.00x  |
| 9  | 0.051      | 0.040      | **1.28x** |
| 10 | 0.154      | 0.140      | **1.10x** |
| 11 | 0.629      | 0.590      | **1.07x** |
| 12 | 3.114      | 3.077      | 1.01x  |
| 13 | 17.617     | 17.536     | 1.00x  |

N≤10 时操作间并行有明显收益（Phase 1+2 构建占总时比例较高）；
N≥12 时 Phase 3（顺序 AND 折叠）占比超过 97%，操作间并行收益趋近于零。

### 操作内并行扩展性（串行版，w=1 → w=6）

| N  | w=1 (s) | w=6 (s) | 扩展比 |
|----|---------|---------|--------|
| 9  | 0.123   | 0.051   | **2.41x** |
| 10 | 0.489   | 0.154   | **3.18x** |
| 11 | 2.329   | 0.629   | **3.70x** |
| 12 | 12.377  | 3.114   | **3.98x** |
| 13 | 75.547  | 17.617  | **4.29x** |

操作内并行对大 N 展现出接近线性的扩展性（6 workers 下 N=13 达到 4.29x）。

## License

Apache-2.0 License, see [LICENSE](LICENSE).
