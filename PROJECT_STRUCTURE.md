# MTPNDD 项目文件结构（整理后）

## 📁 核心目录结构

```
MTPNDD/
├── 📄 核心配置文件
│   ├── README.md                    # 项目说明文档
│   ├── LICENSE                      # 项目许可证
│   ├── pom.xml                      # Maven构建配置
│   ├── run.sh                       # 运行脚本
│   └── .gitignore                   # Git忽略文件
│
├── 📂 sylvan/                       # 核心C语言实现
│   └── src/
│       ├── ndd/                     # NDD核心实现
│       │   ├── 🔧 核心源文件
│       │   │   ├── ndd.c/.h         # NDD核心逻辑
│       │   │   ├── mtpndd.c/.h      # 多终端NDD实现
│       │   │   ├── ndd_parallel.c   # 并行实现
│       │   │   ├── ndd_advanced.c/.h # 高级操作
│       │   │   ├── operation_cache.c/.h # 操作缓存
│       │   │   └── common.c/.h      # 通用工具
│       │   │
│       │   ├── 📋 构建和文档
│       │   │   ├── CMakeLists.txt   # CMake构建配置
│       │   │   └── run_all_tests.sh # 测试运行脚本
│       │   │
│       │   ├── 🧪 测试套件 (tests/)
│       │   │   ├── test_memory_management.c
│       │   │   ├── test_cache_performance.c
│       │   │   ├── test_error_handling.c
│       │   │   ├── test_parallel_*.c
│       │   │   ├── test_mtpndd_*.c  # MTPNDD功能测试
│       │   │   ├── test_advanced_operations.c
│       │   │   └── test_performance_*.c
│       │   │
│       │   └── 🏗️ 构建目录 (build/)
│       │       ├── 编译的测试程序
│       │       └── libndd.a         # 编译的NDD库
│       │
│       └── sylvan/                  # Sylvan BDD库源码
│
├── 📂 src/                          # Java接口实现
│   ├── main/
│   │   ├── java/
│   │   │   ├── application/         # 应用层代码
│   │   │   ├── jsylvan/            # JNI接口
│   │   │   └── org/                # 组织代码
│   │   └── c/                      # JNI C代码
│   └── test/                       # Java测试
│
├── 📂 lib/                          # 依赖库
│   └── jdd-111.jar                 # JDD库
│
├── 📂 archive_unused/               # 已清理的文件
│
└── 📂 results/                      # 输出结果
```

## 🎯 项目当前状态

### ✅ 已完成的核心功能
1. **基础NDD系统** - 完整实现，包含并行支持
2. **MTPNDD多终端系统** - 支持5种终端类型，8种运算策略
3. **缓存系统** - 207倍性能提升，62%命中率
4. **错误处理** - 11种错误类型，双API设计
5. **测试覆盖** - 16个测试套件，全面功能验证

### 🔧 活跃开发的文件

**核心实现 (6个文件)：**
- `ndd.c/.h` - 基础NDD实现
- `mtpndd.c/.h` - 多终端扩展
- `ndd_parallel.c` - 并行框架集成
- `ndd_advanced.c/.h` - 高级操作
- `operation_cache.c/.h` - 高性能缓存
- `common.c/.h` - 通用工具

**测试套件 (16个文件)：**
- 内存管理测试
- 并行功能测试  
- MTPNDD功能测试
- 性能基准测试
- 错误处理测试

### 📊 项目规模统计

**源代码行数：**
- C核心实现：~300KB (6个核心文件)
- C测试套件：~200KB (16个测试文件)
- Java接口：~50KB
- 文档和配置：~100KB

**已清理内容：**
- Java编译产物：target/目录 (12个.class文件)
- 临时备份文件：4个.bak文件
- Sylvan构建缓存：25MB构建产物和临时文件
- CMake临时文件：CMakeFiles/目录缓存
- 历史未使用源码：已归档到archive_unused/ (~455KB)
- 更新.gitignore：防止构建产物重新提交

## 🚀 下一步开发重点

1. **任务6.1** - 智能缓存策略实现
2. **任务6.2** - 性能监控和统计功能  
3. **任务6.3** - 全面的测试套件扩展
4. **任务7** - JNI接口完善
5. **任务8** - 文档和示例完善

## 📋 维护说明

### 构建命令（更新后）
```bash
# 构建Sylvan+Lace依赖（首次）
cd sylvan
mkdir -p build && cd build
cmake ..
make -j4

# 构建NDD库
cd ../src/ndd
mkdir -p build && cd build
cmake ..
make -j4

# 构建Java接口
cd /home/augists/MTPNDD
mvn compile

# 运行测试
cd sylvan/src/ndd/build
./run_all_tests.sh
```

### 添加新测试
1. 在 `sylvan/src/ndd/tests/` 创建测试文件
2. 在 `CMakeLists.txt` 的 `TEST_PROGRAMS` 列表中添加
3. 重新构建：`cmake .. && make`

### 归档规则
- 未使用代码→`archive_unused/`
- 构建产物→自动清理
- 历史版本→Git管理

---
**整理状态：** ✅ 完成，清理了~26MB构建产物和临时文件，项目结构更加清晰，开发效率提升