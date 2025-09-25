# MTPNDD 项目架构文档

## 📋 项目概述

MTPNDD (Multi-Terminal Parallel Network Decision Diagram) 是一个支持多终端节点的高性能并行网络决策图库，基于Lace工作窃取框架和Sylvan并行BDD库构建。

## 🏗️ 整体架构层次

```
┌─────────────────────────────────────────────────────────────┐
│                    MTPNDD 应用层                            │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │   MTPNDD API    │  │   NDD API       │  │  测试程序    │ │
│  │  (多终端支持)    │  │  (基础NDD)      │  │             │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                    核心实现层                               │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │   mtpndd.c      │  │   ndd.c         │  │ ndd_advanced │ │
│  │   mtpndd.h      │  │   ndd.h         │  │    .c/.h     │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │ ndd_nodetable   │  │ operation_cache │  │  common.c/.h │ │
│  │    .c/.h        │  │    .c/.h        │  │              │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                    并行框架层                               │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │   ndd_parallel  │  │   Lace 框架     │  │  Sylvan BDD  │ │
│  │     .c/.h       │  │  (工作窃取)     │  │   (并行BDD)  │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                    系统层                                   │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │   pthread       │  │   C11 标准库    │  │  数学库      │ │
│  │  (线程管理)     │  │  (内存管理)     │  │  (-lm)       │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## 🔧 核心组件详解

### 1. Lace 框架层
**位置**: `_deps/lace-build/`
**作用**: 提供工作窃取并行任务调度框架

```c
// 核心功能
- 工作窃取任务队列管理
- 多线程任务调度
- 并行任务同步机制
- 线程池管理
```

**关键接口**:
- `lace_init()` - 初始化Lace框架
- `lace_cleanup()` - 清理Lace框架
- `TASK_DECL` - 声明并行任务宏

### 2. Sylvan BDD 库
**位置**: `src/sylvan/`
**作用**: 提供高性能并行BDD操作

```c
// 核心功能
- 并行BDD节点管理
- BDD操作缓存系统
- 多线程BDD计算
- 内存优化管理
```

**关键接口**:
- `sylvan_and()` - 并行BDD AND操作
- `sylvan_or()` - 并行BDD OR操作
- `sylvan_not()` - 并行BDD NOT操作
- `sylvan_init()` - 初始化Sylvan

### 3. NDD 核心层
**位置**: `src/ndd/ndd.c`, `src/ndd/ndd.h`

#### 3.1 数据结构设计
```c
// 统一的核心节点结构
typedef struct ndd_node_s {
    uint32_t field;              // 字段ID
    hash_table_t *edges_map;     // 边存储：Map<ndd_node_t*, ndd_bdd_t>
    uint32_t edge_count;         // 当前边数量
    uint32_t ref_count;          // 引用计数
} ndd_node_t;

// NDD句柄类型
typedef struct ndd_s {
    ndd_node_t *node;
} ndd_t;
```

#### 3.2 核心功能
- **节点管理**: 创建、销毁、引用计数
- **边操作**: 基于哈希表的边存储和查找
- **逻辑操作**: AND/OR/NOT/DIFF/EXIST
- **编码操作**: 前缀编码、BDD转换
- **内存管理**: 线程安全的引用计数

### 4. MTPNDD 扩展层
**位置**: `src/ndd/mtpndd.c`, `src/ndd/mtpndd.h`

#### 4.1 多终端类型系统
```c
// 终端值类型枚举
typedef enum mtpndd_terminal_type_e {
    MTPNDD_TERMINAL_BOOLEAN = 0,    // 布尔值（兼容现有NDD）
    MTPNDD_TERMINAL_INTEGER,        // 64位整数  
    MTPNDD_TERMINAL_DOUBLE,         // 双精度浮点数
    MTPNDD_TERMINAL_STRING,         // 字符串
    MTPNDD_TERMINAL_BYTES,          // 二进制数据
    MTPNDD_TERMINAL_CUSTOM          // 自定义类型
} mtpndd_terminal_type_t;

// 终端值联合体
typedef union mtpndd_terminal_value_u {
    bool boolean;
    int64_t integer;
    double floating;
    mtpndd_string_t string;
    mtpndd_bytes_t bytes;
    mtpndd_custom_t custom;
} mtpndd_terminal_value_t;
```

#### 4.2 多终端逻辑操作
- **mtpndd_and()** - 多终端AND操作
- **mtpndd_or()** - 多终端OR操作
- **mtpndd_not()** - 多终端NOT操作
- **mtpndd_ite()** - 条件分支操作

### 5. 并行集成层
**位置**: `src/ndd/ndd_parallel.c`

#### 5.1 并行BDD操作包装
```c
// 真正的并行 BDD 操作包装
ndd_bdd_t ndd_bdd_and_parallel(ndd_bdd_t a, ndd_bdd_t b) {
    return sylvan_and(a, b);
}

ndd_bdd_t ndd_bdd_or_parallel(ndd_bdd_t a, ndd_bdd_t b) {
    return sylvan_or(a, b);
}

ndd_bdd_t ndd_bdd_not_parallel(ndd_bdd_t a) {
    return sylvan_not(a);
}
```

#### 5.2 并行框架管理
- Lace框架初始化和清理
- Sylvan BDD库初始化和清理
- 并行状态管理
- 线程安全保证

### 6. 节点表管理
**位置**: `src/ndd/ndd_nodetable.c`, `src/ndd/ndd_nodetable.h`

#### 6.1 NodeTable 设计
```c
// 按字段分组的节点表
static hash_table_t **g_node_tables_by_field;
static uint32_t g_node_tables_capacity;
```

#### 6.2 节点去重机制
- **ndd_intern_node()** - 节点驻留（去重）
- **ndd_edges_hash_unordered()** - 边集哈希计算
- **ndd_edges_equal()** - 边集等价性比较
- **ndd_mk()** - 统一的节点创建接口

### 7. 通用容器层
**位置**: `src/ndd/common.c`, `src/ndd/common.h`

#### 7.1 基础数据结构
- **hash_table_t** - 通用哈希表
- **dynamic_array_t** - 动态数组
- **hash_set_t** - 哈希集合
- **int_hash_table_t** - 整数键哈希表

#### 7.2 工具函数
- 哈希函数（ptr_hash, int_hash）
- 比较函数（ptr_compare）
- 内存管理函数

## 🔄 数据流和调用关系

### 1. 初始化流程
```
ndd_init() 
  → ndd_lace_init()     // 初始化Lace框架
  → sylvan_init()       // 初始化Sylvan BDD
  → ndd_nodetable_init() // 初始化节点表
  → operation_cache_create() // 创建操作缓存
```

### 2. 节点创建流程
```
ndd_mk(field, edges_map)
  → ndd_intern_node()   // 检查节点是否已存在
  → 如果存在: 返回现有节点
  → 如果不存在: 创建新节点并加入NodeTable
```

### 3. 逻辑操作流程
```
ndd_and(a, b)
  → 检查缓存
  → 遍历edges_map构建新边集
  → 调用ndd_bdd_and_parallel() // 真正的Sylvan并行操作
  → ndd_mk()创建结果节点
  → 更新缓存
```

### 4. 多终端操作流程
```
mtpndd_and(a, b, config)
  → 检查终端类型
  → 调用相应的运算策略
  → 使用ndd_and()进行底层操作
  → 构造多终端结果
```

## 🎯 架构特点

### 1. 统一性 (Unified Architecture)
- **单一数据类型**: 统一使用`ndd_t`作为核心句柄
- **统一接口**: 消除串行/并行API差异
- **统一内存管理**: 线程安全的引用计数系统

### 2. 并行性 (Parallel Processing)
- **双层并行**: Lace任务调度 + Sylvan BDD并行
- **工作窃取**: Lace框架提供高效的任务分配
- **无锁设计**: 尽可能减少锁竞争

### 3. 可扩展性 (Extensibility)
- **多终端支持**: 支持任意类型的终端值
- **自定义类型**: 用户可定义自己的终端类型
- **策略模式**: 可配置的运算策略

### 4. 性能优化 (Performance)
- **节点去重**: NodeTable确保结构等价节点复用
- **操作缓存**: 缓存常用操作结果
- **内存池**: 减少频繁内存分配

## 📊 文件组织结构

```
sylvan/src/ndd/
├── common.h/c              # 通用容器和工具
├── ndd.h/c                 # NDD核心实现
├── ndd_nodetable.h/c       # 节点表管理
├── ndd_parallel.h/c        # 并行框架集成
├── ndd_advanced.h/c        # 高级NDD操作
├── mtpndd.h/c              # 多终端NDD实现
├── operation_cache.h/c     # 操作缓存系统
├── ndd_performance_monitor.h/c # 性能监控
└── CMakeLists.txt          # 构建配置
```

## 🔧 构建和依赖

### 依赖关系
- **Lace**: 工作窃取并行框架
- **Sylvan**: 并行BDD库
- **pthread**: POSIX线程库
- **math**: 数学库 (-lm)

### 构建配置
```cmake
# 自动检测Lace和Sylvan
if(TARGET lace::lace AND TARGET sylvan::sylvan)
    set(HAVE_LACE_SYLVAN TRUE)
else()
    set(HAVE_LACE_SYLVAN FALSE)
endif()
```

## 🎉 总结

MTPNDD项目采用了分层、模块化的架构设计，通过Lace和Sylvan的集成实现了真正的并行计算能力。整个架构具有高度的统一性、可扩展性和性能优化，为网络决策图的应用提供了强大的技术基础。

**核心优势**:
- ✅ 真正的并行计算（Lace + Sylvan）
- ✅ 统一的数据结构和API
- ✅ 多终端节点支持
- ✅ 高性能和可扩展性
- ✅ 完整的错误处理和内存管理
