// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "mtpndd.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

void test_basic_logic_operations() {
    printf("Test basic logic operations\n");
    
    // 初始化MTPNDD系统
    mtpndd_config_t config = {
        .base = {
            .n_workers = 2,
            .dqsize = 10000,
            .table_size = 1 << 16,
            .cache_size = 1 << 12,
            .gc_threshold = 50
        },
        .max_string_length = 1024,
        .max_bytes_length = 1024,
        .enable_string_interning = false,
        .enable_terminal_cache = false
    };
    
    assert(mtpndd_init(&config) == 0);
    printf("MTPNDD system initialized successfully\n");
    
    printf("1. Testing boolean logic operations...\n");
    
    // 创建布尔终端节点
    mtpndd_t true_node = mtpndd_from_boolean(true);
    mtpndd_t false_node = mtpndd_from_boolean(false);
    
    assert(true_node.node != NULL);
    assert(false_node.node != NULL);
    assert(mtpndd_is_boolean_terminal(true_node));
    assert(mtpndd_is_boolean_terminal(false_node));
    
    // 测试AND操作
    mtpndd_t and_result1 = mtpndd_and(true_node, true_node);
    assert(mtpndd_is_boolean_terminal(and_result1));
    assert(mtpndd_get_boolean(and_result1) == true);
    printf("   true AND true = true ✓\n");
    
    mtpndd_t and_result2 = mtpndd_and(true_node, false_node);
    assert(mtpndd_is_boolean_terminal(and_result2));
    assert(mtpndd_get_boolean(and_result2) == false);
    printf("   true AND false = false ✓\n");
    
    // 测试OR操作
    mtpndd_t or_result1 = mtpndd_or(true_node, false_node);
    assert(mtpndd_is_boolean_terminal(or_result1));
    assert(mtpndd_get_boolean(or_result1) == true);
    printf("   true OR false = true ✓\n");
    
    mtpndd_t or_result2 = mtpndd_or(false_node, false_node);
    assert(mtpndd_is_boolean_terminal(or_result2));
    assert(mtpndd_get_boolean(or_result2) == false);
    printf("   false OR false = false ✓\n");
    
    // 测试NOT操作
    mtpndd_t not_result1 = mtpndd_not(true_node);
    assert(mtpndd_is_boolean_terminal(not_result1));
    assert(mtpndd_get_boolean(not_result1) == false);
    printf("   NOT true = false ✓\n");
    
    mtpndd_t not_result2 = mtpndd_not(false_node);
    assert(mtpndd_is_boolean_terminal(not_result2));
    assert(mtpndd_get_boolean(not_result2) == true);
    printf("   NOT false = true ✓\n");
    
    // 清理
    mtpndd_deref(true_node);
    mtpndd_deref(false_node);
    mtpndd_deref(and_result1);
    mtpndd_deref(and_result2);
    mtpndd_deref(or_result1);
    mtpndd_deref(or_result2);
    mtpndd_deref(not_result1);
    mtpndd_deref(not_result2);
    
    mtpndd_quit();
    printf("Basic logic operations test completed\n\n");
}

void test_numeric_logic_operations() {
    printf("Test numeric logic operations\n");
    
    mtpndd_config_t config = {
        .base = {
            .n_workers = 2,
            .dqsize = 10000,
            .table_size = 1 << 16,
            .cache_size = 1 << 12,
            .gc_threshold = 50
        },
        .max_string_length = 1024,
        .max_bytes_length = 1024,
        .enable_string_interning = false,
        .enable_terminal_cache = false
    };
    
    assert(mtpndd_init(&config) == 0);
    printf("MTPNDD system initialized successfully\n");
    
    printf("1. Testing integer numeric operations...\n");
    
    // 创建整数终端节点
    mtpndd_t int_node1 = mtpndd_create_integer_node(10);
    mtpndd_t int_node2 = mtpndd_create_integer_node(20);
    
    assert(int_node1.node != NULL);
    assert(int_node2.node != NULL);
    assert(mtpndd_is_integer_terminal(int_node1));
    assert(mtpndd_is_integer_terminal(int_node2));
    
    // 测试最小值策略
    mtpndd_logic_config_t min_config = mtpndd_create_default_logic_config(MTPNDD_STRATEGY_NUMERIC_MIN);
    mtpndd_t min_result = mtpndd_and_with_config(int_node1, int_node2, &min_config);
    
    assert(mtpndd_is_integer_terminal(min_result));
    assert(mtpndd_get_integer(min_result) == 10);  // min(10, 20) = 10
    printf("   min(10, 20) = 10 ✓\n");
    
    // 测试最大值策略
    mtpndd_logic_config_t max_config = mtpndd_create_default_logic_config(MTPNDD_STRATEGY_NUMERIC_MAX);
    mtpndd_t max_result = mtpndd_or_with_config(int_node1, int_node2, &max_config);
    
    assert(mtpndd_is_integer_terminal(max_result));
    assert(mtpndd_get_integer(max_result) == 20);  // max(10, 20) = 20
    printf("   max(10, 20) = 20 ✓\n");
    
    // 测试加法策略
    mtpndd_logic_config_t add_config = mtpndd_create_default_logic_config(MTPNDD_STRATEGY_NUMERIC_ADD);
    mtpndd_t add_result = mtpndd_and_with_config(int_node1, int_node2, &add_config);
    
    assert(mtpndd_is_integer_terminal(add_result));
    assert(mtpndd_get_integer(add_result) == 30);  // 10 + 20 = 30
    printf("   add(10, 20) = 30 ✓\n");
    
    // 测试乘法策略
    mtpndd_logic_config_t mul_config = mtpndd_create_default_logic_config(MTPNDD_STRATEGY_NUMERIC_MUL);
    mtpndd_t mul_result = mtpndd_and_with_config(int_node1, int_node2, &mul_config);
    
    assert(mtpndd_is_integer_terminal(mul_result));
    assert(mtpndd_get_integer(mul_result) == 200);  // 10 * 20 = 200
    printf("   mul(10, 20) = 200 ✓\n");
    
    // 清理
    mtpndd_deref(int_node1);
    mtpndd_deref(int_node2);
    mtpndd_deref(min_result);
    mtpndd_deref(max_result);
    mtpndd_deref(add_result);
    mtpndd_deref(mul_result);
    
    mtpndd_quit();
    printf("Numeric logic operations test completed\n\n");
}

void test_ite_operations() {
    printf("Test if-then-else operations\n");
    
    mtpndd_config_t config = {
        .base = {
            .n_workers = 2,
            .dqsize = 10000,
            .table_size = 1 << 16,
            .cache_size = 1 << 12,
            .gc_threshold = 50
        },
        .max_string_length = 1024,
        .max_bytes_length = 1024,
        .enable_string_interning = false,
        .enable_terminal_cache = false
    };
    
    assert(mtpndd_init(&config) == 0);
    printf("MTPNDD system initialized successfully\n");
    
    printf("1. Testing boolean condition ITE...\n");
    
    // 创建条件和分支节点
    mtpndd_t true_cond = mtpndd_from_boolean(true);
    mtpndd_t false_cond = mtpndd_from_boolean(false);
    mtpndd_t then_branch = mtpndd_create_integer_node(100);
    mtpndd_t else_branch = mtpndd_create_integer_node(200);
    
    // 测试true条件的ITE
    mtpndd_t ite_result1 = mtpndd_ite(true_cond, then_branch, else_branch);
    assert(mtpndd_is_integer_terminal(ite_result1));
    assert(mtpndd_get_integer(ite_result1) == 100);
    printf("   ITE(true, 100, 200) = 100 ✓\n");
    
    // 测试false条件的ITE
    mtpndd_t ite_result2 = mtpndd_ite(false_cond, then_branch, else_branch);
    assert(mtpndd_is_integer_terminal(ite_result2));
    assert(mtpndd_get_integer(ite_result2) == 200);
    printf("   ITE(false, 100, 200) = 200 ✓\n");
    
    printf("2. Testing integer condition ITE...\n");
    
    // 创建整数条件
    mtpndd_t zero_cond = mtpndd_create_integer_node(0);
    mtpndd_t nonzero_cond = mtpndd_create_integer_node(5);
    
    // 测试零条件的ITE（false）
    mtpndd_t ite_result3 = mtpndd_ite(zero_cond, then_branch, else_branch);
    assert(mtpndd_is_integer_terminal(ite_result3));
    assert(mtpndd_get_integer(ite_result3) == 200);
    printf("   ITE(0, 100, 200) = 200 ✓\n");
    
    // 测试非零条件的ITE（true）
    mtpndd_t ite_result4 = mtpndd_ite(nonzero_cond, then_branch, else_branch);
    assert(mtpndd_is_integer_terminal(ite_result4));
    assert(mtpndd_get_integer(ite_result4) == 100);
    printf("   ITE(5, 100, 200) = 100 ✓\n");
    
    // 清理
    mtpndd_deref(true_cond);
    mtpndd_deref(false_cond);
    mtpndd_deref(then_branch);
    mtpndd_deref(else_branch);
    mtpndd_deref(ite_result1);
    mtpndd_deref(ite_result2);
    mtpndd_deref(zero_cond);
    mtpndd_deref(nonzero_cond);
    mtpndd_deref(ite_result3);
    mtpndd_deref(ite_result4);
    
    mtpndd_quit();
    printf("ITE operations test completed\n\n");
}

void test_logic_statistics() {
    printf("Test logic operation statistics\n");
    
    mtpndd_config_t config = {
        .base = {
            .n_workers = 2,
            .dqsize = 10000,
            .table_size = 1 << 16,
            .cache_size = 1 << 12,
            .gc_threshold = 50
        },
        .max_string_length = 1024,
        .max_bytes_length = 1024,
        .enable_string_interning = false,
        .enable_terminal_cache = false
    };
    
    assert(mtpndd_init(&config) == 0);
    printf("MTPNDD system initialized successfully\n");
    
    // 重置统计信息
    mtpndd_reset_logic_stats();
    
    printf("1. Performing multiple operations...\n");
    
    // 执行多个逻辑操作
    mtpndd_t node1 = mtpndd_from_boolean(true);
    mtpndd_t node2 = mtpndd_from_boolean(false);
    mtpndd_t node3 = mtpndd_create_integer_node(42);
    
    for (int i = 0; i < 10; i++) {
        mtpndd_t and_result = mtpndd_and(node1, node2);
        mtpndd_t or_result = mtpndd_or(node1, node2);
        mtpndd_t not_result = mtpndd_not(node1);
        mtpndd_t ite_result = mtpndd_ite(node1, node2, node3);
        
        mtpndd_deref(and_result);
        mtpndd_deref(or_result);
        mtpndd_deref(not_result);
        mtpndd_deref(ite_result);
    }
    
    printf("2. Checking statistics...\n");
    
    // 获取统计信息
    mtpndd_logic_stats_t stats = mtpndd_get_logic_stats();
    
    assert(stats.and_operations == 10);
    assert(stats.or_operations == 10);
    assert(stats.not_operations == 10);
    assert(stats.ite_operations == 10);
    
    printf("   AND operations: %lu ✓\n", stats.and_operations);
    printf("   OR operations: %lu ✓\n", stats.or_operations);
    printf("   NOT operations: %lu ✓\n", stats.not_operations);
    printf("   ITE operations: %lu ✓\n", stats.ite_operations);
    
    // 打印详细统计信息
    printf("3. Detailed statistics:\n");
    mtpndd_print_logic_stats();
    
    // 清理
    mtpndd_deref(node1);
    mtpndd_deref(node2);
    mtpndd_deref(node3);
    
    mtpndd_quit();
    printf("Logic statistics test completed\n\n");
}

int main() {
    printf("=== MTPNDD Multi-Terminal Logic Operations Test Suite ===\n\n");
    
    test_basic_logic_operations();
    test_numeric_logic_operations();
    test_ite_operations();
    test_logic_statistics();
    
    printf("All MTPNDD logic operations tests passed!\n");
    printf("\nTest Conclusions:\n");
    printf("- Boolean logic operations work correctly\n");
    printf("- Numeric operations support multiple strategies\n");
    printf("- If-then-else operations handle different condition types\n");
    printf("- Logic operation statistics are accurately tracked\n");
    
    printf("\nLogic Operation Features:\n");
    printf("- Multiple terminal value types (boolean, integer, double, string)\n");
    printf("- Configurable operation strategies (min/max/add/mul/concat)\n");
    printf("- Type-safe operations with automatic compatibility checking\n");
    printf("- Comprehensive statistics and performance monitoring\n");
    printf("- Full compatibility with existing NDD operations\n");
    
    return 0;
}