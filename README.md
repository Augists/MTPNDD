# MTPNDD: A library for Multi-Terminal Network Decision Diagram in **Parallel**

使用 C 语言重构 NDD，并实现并行和多终端节点需求

## Architecture

![architecture](docs/mtpndd_architecture_en.png)

## Build and Run

* Sphinx for doc generating

https://www.sphinx-doc.org/en/master/usage/installation.html

for macOS,

```bash
brew install sphinx-doc
```

### MTPNDD with Sylvan and Lace

```bash
cd sylvan
mkdir build
cd build
cmake .. #-DMTPNDD_ENABLE_RECORDING=ON
make
```

### JNI

```bash
cd jni
cmake -B build -DMTPNDD_LIBRARY_PATH=/absolute/path/to/libmtpndd.a
cmake --build build      # 得到 build/libmtpnddjni.so

mvn -DskipTests package  # 产出 target/mtpndd-java-0.1.0-SNAPSHOT.jar
mvn test                 # 运行 JNI 层的单元测试（需要访问 Maven Central）

离线环境可以直接使用 `src/test/java/org/ants/mtpndd/ManualNativeCheck` 进行冒烟验证：

```bash
cd jni
cmake --build build
/usr/local/java/jdk-23/bin/javac -d manual-build/classes \
    $(find src/main/java -name '*.java')
/usr/local/java/jdk-23/bin/javac -cp manual-build/classes -d manual-build/test-classes \
    src/test/java/org/ants/mtpndd/ManualNativeCheck.java \
    src/test/java/org/ants/mtpndd/NQueensMTPNDD.java
/usr/local/java/jdk-23/bin/java -cp manual-build/classes:manual-build/test-classes \
    org.ants.mtpndd.ManualNativeCheck
/usr/local/java/jdk-23/bin/java -cp manual-build/classes:manual-build/test-classes \
    org.ants.mtpndd.NQueensMTPNDD 8
```
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
    .edge_bucket_count = 32,           // 默认为 8
    .nodetable_bucket_count = 1 << 12, // 默认为 1024 或 65537 (LARGE_NODETABLE)
    .gc_bucket_count = 1 << 12,        // 默认为 1024 或 65537
    .node_slab_capacity = 1024,        // 节点池每个 slab 的对象数量，默认 1024
    .edge_entry_slab_capacity = 4096,  // 边条目池每个 slab 的对象数量，默认 4096
    .nodetable_entry_slab_capacity = 2048, // 节点表 entry 池每个 slab 的对象数量，默认 2048
    .edge_map_slab_capacity = 2048    // 边映射结构池每个 slab 的对象数量，默认 2048
};
mtpndd_init(&config);
```

未配置时会自动使用默认值，适合小规模问题。对于节点/边数量巨大的场景，可以按需增大桶数量或调节 slab 大小以降低哈希冲突和频繁分配的开销。

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

## License

Apache-2.0 License, see [LICENSE](LICENSE).
