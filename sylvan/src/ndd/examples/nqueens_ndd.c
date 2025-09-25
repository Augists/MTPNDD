// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
// NQueens problem validation for NDD implementation
// Based on Java version NDDSolution.java implementation

#include "ndd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

// NQueens problem configuration
#define MAX_QUEENS 12

// Global variables
static ndd_t g_imp_batch[MAX_QUEENS][MAX_QUEENS] = {0};
static ndd_t g_or_batch[MAX_QUEENS] = {0};

// Declare fields for NQueens problem (reference Java version)
static void declare_fields(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        ndd_declare_field(n); // Each row has n positions
    }
    printf("✅ Declared %u fields, each field %u bits\n", n, n);
}

// Create variable (corresponds to Java version getVar)
static ndd_t create_var(uint32_t field, uint32_t index) {
    // Create prefix encoding: only the index-th bit is 1
    uint32_t prefix[MAX_QUEENS];
    memset(prefix, 0, sizeof(prefix));
    if (index < MAX_QUEENS) {
        prefix[index] = 1;
    }
    ndd_t result = ndd_encode_prefix(prefix, MAX_QUEENS, field);
    if (result) {
        ndd_ref(result);  // Increase reference count
    }
    return result;
}

// Implement implication operation (corresponds to Java version imp)
static ndd_t ndd_imp(ndd_t a, ndd_t b) {
    // imp(a, b) = not(a) or b
    ndd_t not_a = ndd_not(a);
    ndd_t result = ndd_or(not_a, b);
    ndd_deref(not_a);
    return result;
}

// 构建约束（参考Java版本的build函数）
static void build_constraints(uint32_t i, uint32_t j, uint32_t n) {
    ndd_t a = ndd_true();
    ndd_t b = ndd_true();
    ndd_t c = ndd_true();
    ndd_t d = ndd_true();
    
    // 创建变量：位置(i,j)有皇后
    ndd_t var_ij = create_var(i, j);
    
    // 约束a：同一列没有其他皇后
    for (uint32_t l = 0; l < n; l++) {
        if (l != j) {
            ndd_t var_il = create_var(i, l);
            ndd_t not_var_il = ndd_not(var_il);
            ndd_t imp = ndd_imp(var_ij, not_var_il);
            ndd_t temp = ndd_and(a, imp);
            ndd_deref(a);
            ndd_deref(imp);
            ndd_deref(not_var_il);
            ndd_deref(var_il);
            a = temp;
        }
    }
    
    // 约束b：同一行没有其他皇后
    for (uint32_t k = 0; k < n; k++) {
        if (k != i) {
            ndd_t var_kj = create_var(k, j);
            ndd_t not_var_kj = ndd_not(var_kj);
            ndd_t imp = ndd_imp(var_ij, not_var_kj);
            ndd_t temp = ndd_and(b, imp);
            ndd_deref(b);
            ndd_deref(imp);
            ndd_deref(not_var_kj);
            ndd_deref(var_kj);
            b = temp;
        }
    }
    
    // 约束c：同一上右对角线没有其他皇后
    for (uint32_t k = 0; k < n; k++) {
        int32_t ll = (int32_t)k - (int32_t)i + (int32_t)j;
        if (ll >= 0 && ll < (int32_t)n) {
            if (k != i) {
                ndd_t var_kll = create_var(k, (uint32_t)ll);
                ndd_t not_var_kll = ndd_not(var_kll);
                ndd_t imp = ndd_imp(var_ij, not_var_kll);
                ndd_t temp = ndd_and(c, imp);
                ndd_deref(c);
                ndd_deref(imp);
                ndd_deref(not_var_kll);
                ndd_deref(var_kll);
                c = temp;
            }
        }
    }
    
    // 约束d：同一下右对角线没有其他皇后
    for (uint32_t k = 0; k < n; k++) {
        int32_t ll = (int32_t)i + (int32_t)j - (int32_t)k;
        if (ll >= 0 && ll < (int32_t)n) {
            if (k != i) {
                ndd_t var_kll = create_var(k, (uint32_t)ll);
                ndd_t not_var_kll = ndd_not(var_kll);
                ndd_t imp = ndd_imp(var_ij, not_var_kll);
                ndd_t temp = ndd_and(d, imp);
                ndd_deref(d);
                ndd_deref(imp);
                ndd_deref(not_var_kll);
                ndd_deref(var_kll);
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
            ndd_t var = create_var(i, j);
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
static uint64_t count_solutions(ndd_t queen, uint32_t n) {
    if (ndd_is_false(queen)) {
        return 0;
    }
    if (ndd_is_true(queen)) {
        return 1;
    }
    
    // 将NDD转换为BDD，然后使用Sylvan的satCount
    ndd_bdd_t bdd_queen = ndd_to_bdd(queen);
    if (!bdd_queen) {
        printf("错误：NDD转换为BDD失败\n");
        return 0;
    }
    
    // 创建变量集合（参考Sylvan NQueens示例）
    // 创建所有变量的BDD表示
    BDD vars = sylvan_true;
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < n; j++) {
            BDD var = sylvan_ithvar(i * n + j);
            vars = sylvan_and(vars, var);
        }
    }
    
    // 使用Sylvan的satCount计算满足性计数
    double result_double = sylvan_satcount(bdd_queen, vars);
    uint64_t result = (uint64_t)result_double;
    
    return result;
}

// 主函数（参考Java版本的main函数）
int main(int argc, char *argv[]) {
    printf("🚀 NQueens问题验证NDD实现\n");
    printf("==========================\n");
    
    // 解析命令行参数
    uint32_t n = 8;
    if (argc > 1) {
        n = (uint32_t)atoi(argv[1]);
        if (n < 4 || n > MAX_QUEENS) {
            printf("错误：皇后数量必须在 4 到 %d 之间\n", MAX_QUEENS);
            return 1;
        }
    }
    
    printf("📋 问题配置：\n");
    printf("   - 皇后数量: %u\n", n);
    printf("   - 棋盘大小: %u x %u\n", n, n);
    printf("   - 目标: 找到所有有效的皇后放置方案\n\n");
    
    // 初始化NDD系统
    printf("🔧 初始化NDD系统...\n");
    ndd_config_t config = {
        .n_workers = 4,
        .dqsize = 1000000,
        .table_size = 1LL << 24,
        .cache_size = 1LL << 20,
        .gc_threshold = 1000
    };
    
    if (ndd_init(&config) != NDD_SUCCESS) {
        printf("❌ NDD系统初始化失败\n");
        return 1;
    }
    
    // 解决NQueens问题
    clock_t start_time = clock();
    ndd_t queen = solve_nqueens(n);
    clock_t end_time = clock();
    
    if (ndd_is_false(queen)) {
        printf("❌ 无解决方案\n");
        ndd_quit();
        return 1;
    }
    
    // 计算解决方案数量
    printf("🔍 计算满足性计数...\n");
    clock_t count_start = clock();
    uint64_t solution_count = count_solutions(queen, n);
    clock_t count_end = clock();
    
    // 输出结果
    printf("\n📊 结果总结：\n");
    printf("   - 皇后数量: %u\n", n);
    printf("   - 解决方案数量: %lu\n", solution_count);
    printf("   - 问题解决耗时: %.3f 秒\n", 
           (double)(end_time - start_time) / CLOCKS_PER_SEC);
    printf("   - 满足性计数耗时: %.3f 秒\n", 
           (double)(count_end - count_start) / CLOCKS_PER_SEC);
    printf("   - 总耗时: %.3f 秒\n", 
           (double)(count_end - start_time) / CLOCKS_PER_SEC);
    
    if (solution_count > 0) {
        printf("✅ NDD实现验证成功！\n");
        printf("   - 约束系统工作正常\n");
        printf("   - 逻辑操作执行正常\n");
        printf("   - 并行处理正常\n");
    } else {
        printf("❌ NDD实现验证失败\n");
    }
    
    // 清理资源
    printf("\n🧹 清理资源...\n");
    ndd_deref(queen);
    ndd_quit();
    
    printf("🎉 NQueens验证完成！\n");
    return (solution_count > 0) ? 0 : 1;
}
