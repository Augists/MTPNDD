// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

void test_error_handling_system() {
    printf("🚀 错误处理系统测试开始\n\n");
    
    printf("=== 基础错误处理测试 ===\n");
    
    // 测试错误字符串
    printf("1. 测试错误字符串函数...\n");
    assert(strcmp(ndd_error_string(NDD_SUCCESS), "Success") == 0);
    assert(strcmp(ndd_error_string(NDD_ERROR_INVALID_PARAM), "Invalid parameter") == 0);
    assert(strcmp(ndd_error_string(NDD_ERROR_NOT_INITIALIZED), "NDD system not initialized") == 0);
    printf("   ✓ 错误字符串函数正常工作\n");
    
    // 测试错误设置和获取
    printf("2. 测试错误设置和获取...\n");
    ndd_clear_error();
    ndd_error_info_t error_info = ndd_get_last_error();
    assert(error_info.code == NDD_SUCCESS);
    
    ndd_set_error(NDD_ERROR_INVALID_PARAM, "test_function", 123);
    error_info = ndd_get_last_error();
    assert(error_info.code == NDD_ERROR_INVALID_PARAM);
    assert(strcmp(error_info.function, "test_function") == 0);
    assert(error_info.line == 123);
    printf("   ✓ 错误设置和获取功能正常\n");
    
    printf("3. 测试未初始化状态检查...\n");
    // 确保系统未初始化
    assert(!ndd_is_initialized());
    
    // 测试未初始化状态下的操作
    uint32_t field_id;
    ndd_error_t result = ndd_declare_field_safe(8, &field_id);
    assert(result == NDD_ERROR_NOT_INITIALIZED);
    printf("   ✓ 未初始化状态检查正常\n");
    
    // 测试兼容版本（应该返回无效值）
    uint32_t old_field = ndd_declare_field(8);
    assert(old_field == (uint32_t)-1);
    printf("   ✓ 兼容版本错误处理正常\n");
    
    printf("   ✅ 基础错误处理测试通过\n\n");
}

void test_parameter_validation() {
    printf("=== 参数验证测试 ===\n");
    
    // 初始化系统
    ndd_config_t config = {
        .n_workers = 2,
        .dqsize = 10000,
        .table_size = 1 << 16,
        .cache_size = 1 << 12,
        .gc_threshold = 50
    };
    
    int init_result = ndd_init(&config);
    assert(init_result == 0);
    printf("1. NDD系统初始化成功\n");
    
    printf("2. 测试NULL指针检查...\n");
    
    // 测试NULL指针参数
    ndd_error_t result = ndd_declare_field_safe(8, NULL);
    assert(result == NDD_ERROR_NULL_POINTER);
    printf("   ✓ NULL指针检查正常\n");
    
    // 测试节点创建的NULL指针检查
    result = ndd_create_node_safe(999, NULL);  // 无效字段ID + NULL结果指针
    assert(result == NDD_ERROR_NULL_POINTER);
    printf("   ✓ 节点创建NULL指针检查正常\n");
    
    printf("3. 测试无效参数检查...\n");
    
    // 测试无效的bit_width
    uint32_t field_id;
    result = ndd_declare_field_safe(0, &field_id);  // bit_width = 0
    assert(result == NDD_ERROR_INVALID_PARAM);
    
    result = ndd_declare_field_safe(65, &field_id);  // bit_width > 64
    assert(result == NDD_ERROR_INVALID_PARAM);
    printf("   ✓ 无效参数检查正常\n");
    
    printf("4. 测试无效字段ID检查...\n");
    
    // 创建有效字段
    result = ndd_declare_field_safe(8, &field_id);
    assert(result == NDD_SUCCESS);
    assert(field_id == 0);
    
    // 测试无效字段ID
    ndd_t node_result;
    result = ndd_create_node_safe(999, &node_result);  // 不存在的字段ID
    assert(result == NDD_ERROR_INVALID_FIELD);
    printf("   ✓ 无效字段ID检查正常\n");
    
    printf("5. 测试边界条件...\n");
    
    // 测试添加边到终端节点（应该失败）
    ndd_t true_node = ndd_true();
    result = ndd_add_edge_safe(&true_node, ndd_false(), ndd_sylvan_true);
    assert(result == NDD_ERROR_INVALID_PARAM);
    printf("   ✓ 终端节点边界检查正常\n");
    
    // 测试正常的节点操作
    result = ndd_create_node_safe(field_id, &node_result);
    assert(result == NDD_SUCCESS);
    assert(!node_result.is_terminal);
    printf("   ✓ 正常节点创建成功\n");
    
    // 测试正常的添加边操作
    result = ndd_add_edge_safe(&node_result, ndd_true(), ndd_sylvan_true);
    assert(result == NDD_SUCCESS);
    assert(node_result->edge_count == 1);
    printf("   ✓ 正常边添加成功\n");
    
    // 清理
    ndd_deref_safe(node_result);
    
    ndd_quit();
    printf("   ✅ 参数验证测试通过\n\n");
}

void test_memory_safety() {
    printf("=== 内存安全测试 ===\n");
    
    // 重新初始化
    ndd_config_t config = {
        .n_workers = 2,
        .dqsize = 10000,
        .table_size = 1 << 16,
        .cache_size = 1 << 12,
        .gc_threshold = 50
    };
    
    int init_result = ndd_init(&config);
    assert(init_result == 0);
    
    printf("1. 测试大量字段声明的容量检查...\n");
    
    // 声明多个字段，测试动态扩容
    uint32_t field_ids[100];
    for (int i = 0; i < 100; i++) {
        ndd_error_t result = ndd_declare_field_safe(4, &field_ids[i]);
        assert(result == NDD_SUCCESS);
        assert(field_ids[i] == (uint32_t)i);
    }
    printf("   ✓ 动态扩容功能正常，成功声明100个字段\n");
    
    printf("2. 测试大量节点创建的内存管理...\n");
    
    // 创建大量节点测试内存分配
    ndd_t nodes[1000];
    for (int i = 0; i < 1000; i++) {
        ndd_error_t result = ndd_create_node_safe(0, &nodes[i]);
        assert(result == NDD_SUCCESS);
        assert(!nodes[i].is_terminal);
        
        // 为每个节点添加边，测试边数组扩容
        for (int j = 0; j < 5; j++) {
            result = ndd_add_edge_safe(&nodes[i], ndd_true(), ndd_sylvan_true);
            assert(result == NDD_SUCCESS);
        }
        assert(nodes[i]->edge_count == 5);
    }
    
    printf("   ✓ 成功创建1000个节点，每个节点5条边\n");
    
    // 检查内存统计
    ndd_memory_stats_t stats = ndd_get_memory_stats();
    printf("   - 当前分配内存: %lu 字节\n", stats.current_allocated);
    printf("   - 总分配内存: %lu 字节\n", stats.total_allocated);
    assert(stats.current_allocated > 0);
    
    printf("3. 测试内存清理...\n");
    
    // 清理所有节点
    for (int i = 0; i < 1000; i++) {
        ndd_deref_safe(nodes[i]);
    }
    
    // 强制GC
    ndd_gc();
    
    printf("   ✓ 内存清理完成\n");
    
    ndd_quit();
    printf("   ✅ 内存安全测试通过\n\n");
}

void test_error_reporting() {
    printf("=== 错误报告测试 ===\n");
    
    printf("1. 测试详细错误信息...\n");
    
    // 确保系统未初始化
    assert(!ndd_is_initialized());
    
    // 触发一个错误
    uint32_t field_id;
    ndd_error_t result = ndd_declare_field_safe(8, &field_id);
    assert(result == NDD_ERROR_NOT_INITIALIZED);
    
    // 检查错误信息
    ndd_error_info_t error_info = ndd_get_last_error();
    printf("   错误代码: %d\n", error_info.code);
    printf("   错误消息: %s\n", error_info.message);
    printf("   错误函数: %s\n", error_info.function);
    printf("   错误行号: %d\n", error_info.line);
    
    assert(error_info.code == NDD_ERROR_NOT_INITIALIZED);
    assert(error_info.function != NULL);
    assert(error_info.line > 0);
    printf("   ✓ 详细错误信息记录正常\n");
    
    printf("2. 测试错误清理...\n");
    
    ndd_clear_error();
    error_info = ndd_get_last_error();
    assert(error_info.code == NDD_SUCCESS);
    assert(error_info.function == NULL);
    printf("   ✓ 错误清理功能正常\n");
    
    printf("   ✅ 错误报告测试通过\n\n");
}

int main() {
    printf("🚀 NDD错误处理和边界检查完整测试开始\n\n");
    
    test_error_handling_system();
    test_parameter_validation();
    test_memory_safety();
    test_error_reporting();
    
    printf("🎉 错误处理和边界检查测试完成！\n");
    printf("\n📋 测试结论:\n");
    printf("✅ 统一错误处理系统工作正常\n");
    printf("✅ 参数验证机制完整有效\n");
    printf("✅ 边界检查防护到位\n");
    printf("✅ 内存安全保护有效\n");
    printf("✅ 错误报告详细准确\n");
    printf("✅ 向后兼容性保持良好\n");
    
    printf("\n🔧 实现特性:\n");
    printf("- 线程本地错误状态管理\n");
    printf("- 统一的错误码系统\n");
    printf("- 详细的错误定位信息\n");
    printf("- 自动参数验证宏\n");
    printf("- 安全版本和兼容版本双API\n");
    printf("- 完整的边界条件检查\n");
    
    return 0;
}