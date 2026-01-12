# MTPNDD 内存分析工具

本目录包含用于测量和分析 MTPNDD 内存使用的工具脚本。

## 脚本列表

### 1. measure_memory.sh

**用途**: 测量单个基准测试的内存使用情况

**使用方法**:
```bash
# 从项目根目录或 tools/ 目录运行

# 方法 1: /proc 监控（快速，推荐用于日常测试）
./tools/measure_memory.sh proc <executable> [args...]

# 方法 2: Valgrind Massif（精确但慢，用于详细分析）
./tools/measure_memory.sh massif <executable> [args...]
```

**示例**:
```bash
# 测量 NQueens N=12 的内存使用
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1

# 使用 Massif 进行详细分析（较慢，用于 N<=10）
./tools/measure_memory.sh massif ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 10 1
```

**输出**:
- 峰值 RSS (Resident Set Size) - 实际物理内存使用
- 峰值 VMS (Virtual Memory Size) - 总分配内存
- 采样统计信息
- 程序输出（solutions 等）

**说明**:
- `/proc` 方法每 20ms 采样一次，适合快速评估
- `massif` 方法使用 Valgrind，会显著降低执行速度（~20-50x），但提供堆内存的详细分析
- 结果保存在 `/tmp/` 目录（`proc_stdout_*.txt`, `massif.out.*` 等）

---

### 2. memory_compare.sh

**用途**: 批量测试多个 N 值，生成对比报告

**使用方法**:
```bash
# 从项目根目录或 tools/ 目录运行
./tools/memory_compare.sh
```

**功能**:
- 自动测试 N=10, 11, 12（可在脚本中修改 `TEST_SIZES` 数组）
- 为每个测试生成详细报告
- 汇总所有结果到 summary.txt
- 结果保存在 `/tmp/memory_results_<timestamp>/` 目录

**输出示例**:
```
/tmp/memory_results_20260112_112345/
├── feature-index_N10_W1.txt
├── feature-index_N11_W1.txt
├── feature-index_N12_W1.txt
└── summary.txt
```

**修改测试配置**:
编辑 `memory_compare.sh` 中的变量：
```bash
declare -a TEST_SIZES=("10" "11" "12")  # 添加或删除 N 值
WORKER_COUNT=1                           # 修改 worker 数量
```

---

## 使用场景

### 场景 1: 快速评估单个测试的内存使用

```bash
# 1. 构建项目
cmake --build build

# 2. 测量内存
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1

# 查看结果
# 输出: PEAK MEMORY USAGE: XXX.XX MB
```

---

### 场景 2: 对比不同分支的内存使用

```bash
# 1. 测试 feature/index 分支
git checkout feature/index
cmake --build build
./tools/memory_compare.sh
# 记录结果目录，例如: /tmp/memory_results_20260112_112345/

# 2. 测试 feature/opt-compact 分支
git checkout feature/opt-compact
cmake --build build
./tools/memory_compare.sh
# 记录结果目录，例如: /tmp/memory_results_20260112_113456/

# 3. 对比两个 summary.txt
diff /tmp/memory_results_20260112_112345/summary.txt \
     /tmp/memory_results_20260112_113456/summary.txt
```

---

### 场景 3: 性能优化前后的详细对比

```bash
# 优化前：记录基线
git checkout feature/index
cmake --build build
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1 \
    | tee baseline_memory.txt

# 实施优化（例如：紧凑存储）
# ... 修改代码 ...

# 优化后：测试新版本
cmake --build build
./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1 \
    | tee optimized_memory.txt

# 对比结果
echo "=== Baseline ==="
grep "PEAK MEMORY" baseline_memory.txt
echo "=== Optimized ==="
grep "PEAK MEMORY" optimized_memory.txt

# 计算改进
baseline=$(grep "PEAK MEMORY" baseline_memory.txt | awk '{print $4}')
optimized=$(grep "PEAK MEMORY" optimized_memory.txt | awk '{print $4}')
awk "BEGIN {printf \"Improvement: %.1f%%\n\", (($baseline - $optimized) / $baseline) * 100}"
```

---

### 场景 4: 使用 Massif 进行详细堆分析

```bash
# 运行 Massif（较慢，用于小问题）
./tools/measure_memory.sh massif ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 10 1

# Massif 输出文件位置
MASSIF_OUT=$(ls -t /tmp/massif.out.* | head -1)

# 查看详细报告
ms_print "$MASSIF_OUT" | less

# 查看峰值内存快照
ms_print "$MASSIF_OUT" | grep -A 20 "peak"
```

---

## 依赖

### 必需工具

- **bash**: 脚本运行环境
- **awk**: 数据处理
- **grep**: 文本过滤
- **proc 文件系统**: Linux 内存监控（`/proc/PID/status`）

### 可选工具

- **valgrind**: Massif 堆分析（仅 `massif` 模式需要）
  ```bash
  # Arch Linux
  sudo pacman -S valgrind

  # Ubuntu/Debian
  sudo apt install valgrind
  ```

---

## 故障排除

### 问题 1: "No memory samples collected"

**原因**: 进程执行时间太短，采样不到数据

**解决**:
- 增大问题规模（例如从 N=8 改为 N=10）
- 减小采样间隔（编辑脚本，将 `sleep 0.02` 改为 `sleep 0.01`）

---

### 问题 2: Valgrind 太慢

**原因**: Massif 会减慢程序 20-50 倍

**解决**:
- 仅用于小规模测试（N<=10）
- 日常测试使用 `/proc` 方法
- 如需详细分析，可以在测试机上后台运行

---

### 问题 3: 权限错误

**原因**: 脚本没有执行权限

**解决**:
```bash
chmod +x tools/*.sh
```

---

## 输出解释

### RSS (Resident Set Size)
- **定义**: 实际占用的物理内存（RAM）
- **包含**: 代码段、数据段、堆、栈
- **不包含**: 换出到磁盘的页面、未分配的虚拟内存
- **重要性**: 这是真实的内存消耗指标

### VMS (Virtual Memory Size)
- **定义**: 进程的虚拟地址空间总大小
- **包含**: 所有映射的内存区域（包括未使用的）
- **特点**: 通常远大于 RSS
- **用途**: 了解内存映射和预分配情况

### 示例输出
```
Peak RSS (Physical Memory): 790648 KB (772.12 MB)
Peak VMS (Virtual Memory):  6306704 KB (6158.89 MB)
```

**解释**:
- 程序实际使用了 772 MB 物理内存
- 虚拟地址空间分配了 6.1 GB（可能包含 mmap 的大块预分配）
- VMS/RSS 比值 ~8，说明虚拟内存使用较为激进

---

## 进阶使用

### 自定义采样频率

编辑 `measure_memory.sh` 中的 `sleep` 参数：
```bash
# 当前: 20ms 采样间隔
sleep 0.02

# 更快采样（10ms）：更精确但 CPU 占用高
sleep 0.01

# 更慢采样（50ms）：CPU 占用低但可能漏峰值
sleep 0.05
```

### 批量测试多个分支

```bash
#!/bin/bash
# 测试多个分支的内存使用

BRANCHES=("feature/index" "feature/compact" "feature/simd")
RESULTS_DIR="/tmp/branch_comparison_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULTS_DIR"

for branch in "${BRANCHES[@]}"; do
    echo "Testing $branch..."
    git checkout "$branch"
    cmake --build build
    ./tools/measure_memory.sh proc ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1 \
        > "$RESULTS_DIR/${branch}.txt" 2>&1
done

# 汇总结果
for branch in "${BRANCHES[@]}"; do
    echo "=== $branch ==="
    grep "PEAK MEMORY" "$RESULTS_DIR/${branch}.txt"
done
```

---

## 相关文档

- **内存优化分析**: `../docs/memory_optimization_analysis.md` - 详细的优化方向和实施策略
- **内存分析摘要**: `../docs/memory_analysis_summary.md` - 执行摘要和测试结果
- **temp_refs 优化**: `../docs/temp_refs_results.md` - temp_refs 优化的性能提升记录

---

**维护者**: Claude Code Analysis
**最后更新**: 2026-01-12
**版本**: 1.0
