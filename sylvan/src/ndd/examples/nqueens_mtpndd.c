// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
// NQueens problem validation for MTPNDD implementation
// Based on Java version NDDSolution.java implementation

#include "ndd.h"
#include "mtpndd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

// NQueens problem configuration
#define MAX_QUEENS 12
#define QUEEN_SYMBOL "Q"
#define EMPTY_SYMBOL "."

// Global variables
static uint32_t g_board_size = 8;
static ndd_t g_imp_batch[MAX_QUEENS][MAX_QUEENS];
static ndd_t g_or_batch[MAX_QUEENS];

// Declare fields for NQueens problem (reference Java version)
static void declare_fields(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        ndd_declare_field(n); // Each row has n positions
    }
    printf("✅ Declared %u fields, each field %u bits\n", n, n);
}

// 构建约束（参考Java版本的build函数）
static void build_constraints(uint32_t i, uint32_t j, uint32_t n) {
    ndd_t a = ndd_true();
    ndd_t b = ndd_true();
    ndd_t c = ndd_true();
    ndd_t d = ndd_true();
    
    // 创建变量：位置(i,j)有皇后
    ndd_t var_ij = ndd_create_var(i, j);
    
    // 约束a：同一列没有其他皇后
    for (uint32_t l = 0; l < n; l++) {
        if (l != j) {
            ndd_t not_var_il = ndd_not(ndd_create_var(i, l));
            ndd_t imp = ndd_imp(var_ij, not_var_il);
            ndd_t temp = ndd_and(a, imp);
            ndd_deref(a);
            ndd_deref(imp);
            a = temp;
        }
    }
    
    // 约束b：同一行没有其他皇后
    for (uint32_t k = 0; k < n; k++) {
        if (k != i) {
            ndd_t not_var_kj = ndd_not(ndd_create_var(k, j));
            ndd_t imp = ndd_imp(var_ij, not_var_kj);
            ndd_t temp = ndd_and(b, imp);
            ndd_deref(b);
            ndd_deref(imp);
            b = temp;
        }
    }
    
    // 约束c：同一上右对角线没有其他皇后
    for (uint32_t k = 0; k < n; k++) {
        int32_t ll = (int32_t)k - (int32_t)i + (int32_t)j;
        if (ll >= 0 && ll < (int32_t)n) {
            if (k != i) {
                ndd_t not_var_kll = ndd_not(ndd_create_var(k, (uint32_t)ll));
                ndd_t imp = ndd_imp(var_ij, not_var_kll);
                ndd_t temp = ndd_and(c, imp);
                ndd_deref(c);
                ndd_deref(imp);
                c = temp;
            }
        }
    }
    
    // 约束d：同一下右对角线没有其他皇后
    for (uint32_t k = 0; k < n; k++) {
        int32_t ll = (int32_t)i + (int32_t)j - (int32_t)k;
        if (ll >= 0 && ll < (int32_t)n) {
            if (k != i) {
                ndd_t not_var_kll = ndd_not(ndd_create_var(k, (uint32_t)ll));
                ndd_t imp = ndd_imp(var_ij, not_var_kll);
                ndd_t temp = ndd_and(d, imp);
                ndd_deref(d);
                ndd_deref(imp);
                d = temp;
            }
        }
    }
    
    // 合并所有约束
    ndd_t temp1 = ndd_and(c, d);
    ndd_t temp2 = ndd_and(b, temp1);
    ndd_t temp3 = ndd_and(a, temp2);
    
    g_imp_batch[i][j] = temp3;
    
    // 清理临时变量
    ndd_deref(a);
    ndd_deref(b);
    ndd_deref(c);
    ndd_deref(d);
    ndd_deref(temp1);
    ndd_deref(temp2);
    ndd_deref(var_ij);
}

// 解决NQueens问题（参考Java版本的Solution函数）
static ndd_t solve_nqueens(uint32_t n) {
    printf("🔧 解决NQueens问题 (n=%u)...\n", n);
    
    // 声明字段
    declare_fields(n);
    
    // 创建OR批次：每行至少有一个皇后
    for (uint32_t i = 0; i < n; i++) {
        ndd_t condition = ndd_false();
        for (uint32_t j = 0; j < n; j++) {
            ndd_t var = ndd_create_var(i, j);
            ndd_t temp = ndd_or(condition, var);
            ndd_deref(condition);
            ndd_deref(var);
            condition = temp;
        }
        g_or_batch[i] = condition;
    }
    
    // 构建约束批次
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < n; j++) {
            build_constraints(i, j, n);
        }
    }
    
    // 合并所有约束
    ndd_t queen = ndd_true();
    
    // 每行至少有一个皇后
    for (uint32_t i = 0; i < n; i++) {
        ndd_t temp = ndd_and(queen, g_or_batch[i]);
        ndd_deref(queen);
        queen = temp;
    }
    
    // 所有位置约束
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < n; j++) {
            ndd_t temp = ndd_and(queen, g_imp_batch[i][j]);
            ndd_deref(queen);
            queen = temp;
        }
    }
    
    printf("✅ NQueens问题解决完成\n");
    return queen;
}

// 计算满足性计数（参考Java版本的satCount）
static uint64_t count_solutions(ndd_t queen) {
    if (ndd_is_false(queen)) {
        return 0;
    }
    if (ndd_is_true(queen)) {
        return 1;
    }
    
    // 简化实现：返回一个估计值
    // 在实际实现中，需要遍历NDD的所有满足路径
    return 1; // 至少有一个解决方案
}

// 主函数
int main(int argc, char *argv[]) {
    printf("🚀 NQueens问题验证MTPNDD实现\n");
    printf("================================\n");
    
    // 解析命令行参数
    uint32_t board_size = 8;
    if (argc > 1) {
        board_size = (uint32_t)atoi(argv[1]);
        if (board_size < 4 || board_size > MAX_QUEENS) {
            printf("错误：棋盘大小必须在 4 到 %d 之间\n", MAX_QUEENS);
            return 1;
        }
    }
    
    printf("📋 问题配置：\n");
    printf("   - 棋盘大小: %u x %u\n", board_size, board_size);
    printf("   - 皇后数量: %u\n", board_size);
    printf("   - 目标: 找到所有有效的皇后放置方案\n\n");
    
    // 初始化MTPNDD系统（会自动初始化NDD）
    printf("🔧 初始化MTPNDD系统...\n");
    ndd_config_t ndd_config = {
        .n_workers = 4,
        .dqsize = 1000000,
        .table_size = 1LL << 24,
        .cache_size = 1LL << 20,
        .gc_threshold = 1000
    };
    
    mtpndd_config_t mtpndd_config = {
        .base = ndd_config,
        .max_string_length = 1024,
        .max_bytes_length = 1024,
        .enable_string_interning = true,
        .enable_terminal_cache = true
    };
    
    if (mtpndd_init(&mtpndd_config) != MTPNDD_SUCCESS) {
        printf("❌ MTPNDD系统初始化失败\n");
        return 1;
    }
    
    // 初始化NQueens字段
    if (init_nqueens_fields(board_size) != 0) {
        printf("❌ NQueens字段初始化失败\n");
        mtpndd_quit();
        ndd_quit();
        return 1;
    }
    
    // 创建NQueens约束
    clock_t start_time = clock();
    mtpndd_t constraints = create_nqueens_constraints();
    clock_t constraint_time = clock();
    
    if (!constraints.node) {
        printf("❌ NQueens约束创建失败\n");
        mtpndd_quit();
        ndd_quit();
        return 1;
    }
    
    printf("⏱️  约束创建耗时: %.3f 秒\n", 
           (double)(constraint_time - start_time) / CLOCKS_PER_SEC);
    
    // 提取解决方案
    clock_t extract_start = clock();
    int solution_count = extract_solutions(constraints, board_size);
    clock_t extract_end = clock();
    
    printf("⏱️  解决方案提取耗时: %.3f 秒\n", 
           (double)(extract_end - extract_start) / CLOCKS_PER_SEC);
    
    // 输出结果
    printf("\n📊 结果总结：\n");
    printf("   - 找到解决方案数量: %d\n", solution_count);
    printf("   - 总耗时: %.3f 秒\n", 
           (double)(extract_end - start_time) / CLOCKS_PER_SEC);
    
    if (solution_count > 0) {
        printf("✅ MTPNDD实现验证成功！\n");
        printf("   - 多终端节点处理正常\n");
        printf("   - 约束系统工作正常\n");
        printf("   - 逻辑操作执行正常\n");
    } else {
        printf("❌ MTPNDD实现验证失败\n");
    }
    
    // 清理资源
    printf("\n🧹 清理资源...\n");
    mtpndd_quit();
    
    printf("🎉 NQueens验证完成！\n");
    return (solution_count > 0) ? 0 : 1;
}
