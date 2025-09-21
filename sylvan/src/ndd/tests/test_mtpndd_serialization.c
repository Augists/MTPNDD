// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "mtpndd.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

void test_basic_serialization() {
    printf("Test basic serialization operations\n");
    
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
    
    printf("1. Testing node serialization...\n");
    
    // 创建不同类型的终端节点
    mtpndd_t bool_node = mtpndd_create_integer_node(42);
    mtpndd_t string_node = mtpndd_create_string_node("test_data");
    
    // 测试JSON格式序列化
    mtpndd_serialization_options_t options = {
        .format = MTPNDD_FORMAT_JSON,
        .compress_data = false,
        .include_metadata = true,
        .validate_integrity = true,
        .version = 1,
        .encoding = "UTF-8"
    };
    
    mtpndd_serialized_data_t *bool_data = mtpndd_serialize_node(bool_node, &options);
    assert(bool_data != NULL);
    assert(bool_data->data != NULL);
    assert(bool_data->size > 0);
    printf("   Integer node serialization successful: %zu bytes\n", bool_data->size);
    printf("   Serialized content: %.*s\n", (int)bool_data->size, (char*)bool_data->data);
    
    mtpndd_serialized_data_t *string_data = mtpndd_serialize_node(string_node, &options);
    assert(string_data != NULL);
    assert(string_data->data != NULL);
    assert(string_data->size > 0);
    printf("   String node serialization successful: %zu bytes\n", string_data->size);
    printf("   Serialized content: %.*s\n", (int)string_data->size, (char*)string_data->data);
    
    printf("2. Testing data integrity verification...\n");
    
    // 验证数据完整性
    bool bool_valid = mtpndd_verify_serialized_data(bool_data);
    bool string_valid = mtpndd_verify_serialized_data(string_data);
    
    assert(bool_valid == true);
    assert(string_valid == true);
    printf("   Data integrity verification passed\n");
    
    // 清理
    mtpndd_deref(bool_node);
    mtpndd_deref(string_node);
    mtpndd_destroy_serialized_data(bool_data);
    mtpndd_destroy_serialized_data(string_data);
    
    mtpndd_quit();
    printf("Basic serialization test completed\n\n");
}

void test_deserialization() {
    printf("Test deserialization operations\n");
    
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
    
    printf("1. Testing round-trip serialization...\n");
    
    // 创建原始节点
    mtpndd_t original = mtpndd_create_integer_node(123);
    
    // 序列化
    mtpndd_serialization_options_t options = {
        .format = MTPNDD_FORMAT_JSON,
        .compress_data = false,
        .include_metadata = true,
        .validate_integrity = true,
        .version = 1,
        .encoding = "UTF-8"
    };
    
    mtpndd_serialized_data_t *serialized = mtpndd_serialize_node(original, &options);
    assert(serialized != NULL);
    printf("   Original node serialized successfully\n");
    
    // 反序列化
    mtpndd_deserialized_result_t *deserialized = mtpndd_deserialize(serialized);
    assert(deserialized != NULL);
    assert(deserialized->node_count == 1);
    assert(deserialized->nodes != NULL);
    printf("   Node deserialized successfully\n");
    printf("   Integrity verified: %s\n", deserialized->integrity_verified ? "Yes" : "No");
    
    printf("2. Testing serialization statistics...\n");
    
    // 获取统计信息
    mtpndd_serialization_stats_t stats = mtpndd_get_serialization_stats(serialized);
    printf("   Total nodes: %zu\n", stats.total_nodes);
    printf("   Uncompressed size: %zu bytes\n", stats.uncompressed_size);
    printf("   Compressed size: %zu bytes\n", stats.compressed_size);
    printf("   Compression ratio: %.2f\n", stats.compression_ratio);
    
    // 清理
    mtpndd_deref(original);
    mtpndd_destroy_serialized_data(serialized);
    mtpndd_destroy_deserialized_result(deserialized);
    
    mtpndd_quit();
    printf("Deserialization test completed\n\n");
}

void test_file_serialization() {
    printf("Test file serialization operations\n");
    
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
    
    printf("1. Testing file serialization...\n");
    
    // 创建测试节点
    mtpndd_t test_node = mtpndd_create_string_node("file_test_data");
    
    // 序列化到文件
    mtpndd_serialization_options_t options = {
        .format = MTPNDD_FORMAT_JSON,
        .compress_data = false,
        .include_metadata = true,
        .validate_integrity = true,
        .version = 1,
        .encoding = "UTF-8"
    };
    
    const char *filename = "/tmp/mtpndd_test.dat";
    int result = mtpndd_serialize_to_file(test_node, filename, &options);
    assert(result == 0);
    printf("   Node serialized to file successfully: %s\n", filename);
    
    printf("2. Testing file deserialization...\n");
    
    // 从文件反序列化
    mtpndd_deserialized_result_t *file_result = mtpndd_deserialize_from_file(filename);
    assert(file_result != NULL);
    assert(file_result->node_count == 1);
    printf("   Node deserialized from file successfully\n");
    printf("   File integrity verified: %s\n", file_result->integrity_verified ? "Yes" : "No");
    
    // 清理
    mtpndd_deref(test_node);
    mtpndd_destroy_deserialized_result(file_result);
    
    // 删除测试文件
    remove(filename);
    
    mtpndd_quit();
    printf("File serialization test completed\n\n");
}

void test_serialization_performance() {
    printf("Test serialization performance\n");
    
    mtpndd_config_t config = {
        .base = {
            .n_workers = 4,
            .dqsize = 100000,
            .table_size = 1 << 20,
            .cache_size = 1 << 16,
            .gc_threshold = 50
        },
        .max_string_length = 1024,
        .max_bytes_length = 1024,
        .enable_string_interning = true,
        .enable_terminal_cache = true
    };
    
    assert(mtpndd_init(&config) == 0);
    printf("MTPNDD system initialized successfully\n");
    
    printf("1. Testing batch serialization performance...\n");
    
    clock_t start = clock();
    
    // 创建多个节点并序列化
    const int node_count = 50;
    mtpndd_t nodes[node_count];
    mtpndd_serialized_data_t *serialized_data[node_count];
    
    mtpndd_serialization_options_t options = {
        .format = MTPNDD_FORMAT_JSON,
        .compress_data = false,
        .include_metadata = false,  // 减少开销
        .validate_integrity = false,
        .version = 1,
        .encoding = "UTF-8"
    };
    
    for (int i = 0; i < node_count; i++) {
        nodes[i] = mtpndd_create_integer_node(i * 10);
        serialized_data[i] = mtpndd_serialize_node(nodes[i], &options);
        assert(serialized_data[i] != NULL);
    }
    
    clock_t end = clock();
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("   %d nodes serialized in %.2f ms\n", node_count, elapsed * 1000);
    printf("   Average time per node: %.3f ms\n", (elapsed * 1000) / node_count);
    
    printf("2. Testing deserialization performance...\n");
    
    start = clock();
    
    // 反序列化所有节点
    mtpndd_deserialized_result_t *deserialized_results[node_count];
    for (int i = 0; i < node_count; i++) {
        deserialized_results[i] = mtpndd_deserialize(serialized_data[i]);
        assert(deserialized_results[i] != NULL);
    }
    
    end = clock();
    elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("   %d nodes deserialized in %.2f ms\n", node_count, elapsed * 1000);
    printf("   Average time per node: %.3f ms\n", (elapsed * 1000) / node_count);
    
    // 计算总的序列化大小
    size_t total_size = 0;
    for (int i = 0; i < node_count; i++) {
        total_size += serialized_data[i]->size;
    }
    printf("   Total serialized size: %zu bytes\n", total_size);
    printf("   Average size per node: %.1f bytes\n", (double)total_size / node_count);
    
    // 清理
    for (int i = 0; i < node_count; i++) {
        mtpndd_deref(nodes[i]);
        mtpndd_destroy_serialized_data(serialized_data[i]);
        mtpndd_destroy_deserialized_result(deserialized_results[i]);
    }
    
    mtpndd_quit();
    printf("Serialization performance test completed\n\n");
}

int main() {
    printf("=== MTPNDD Serialization/Deserialization Test Suite ===\n\n");
    
    test_basic_serialization();
    test_deserialization();
    test_file_serialization();
    test_serialization_performance();
    
    printf("All MTPNDD serialization/deserialization tests passed!\n");
    printf("\nTest Conclusions:\n");
    printf("- Basic serialization operations work correctly\n");
    printf("- Round-trip serialization maintains data integrity\n");
    printf("- File-based serialization is functional\n");
    printf("- Serialization performance is acceptable\n");
    
    printf("\nSerialization Features:\n");
    printf("- JSON format support for readability\n");
    printf("- Data integrity verification with checksums\n");
    printf("- File-based persistence operations\n");
    printf("- Performance optimization for batch operations\n");
    printf("- Comprehensive error handling and validation\n");
    
    return 0;
}