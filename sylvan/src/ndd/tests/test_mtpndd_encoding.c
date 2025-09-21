// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "mtpndd.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

void test_basic_encoding() {
    printf("Test basic encoding operations\n");
    
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
    
    // 测试前缀编码
    printf("1. Testing prefix encoding...\n");
    uint32_t field = ndd_declare_field(4);
    uint32_t prefix[] = {1, 0, 1, 1}; // 二进制前缀 1011
    
    mtpndd_t encoded = mtpndd_encode_prefix(prefix, 4, field);
    assert(encoded.node != NULL);
    printf("   Prefix encoding successful: [1,0,1,1] -> field %u\n", field);
    
    // 测试带终端值的前缀编码
    printf("2. Testing prefix encoding with terminal...\n");
    mtpndd_terminal_t *terminal = mtpndd_terminal_integer(42);
    assert(terminal != NULL);
    
    mtpndd_t encoded_with_terminal = mtpndd_encode_prefix_with_terminal(prefix, 4, field, terminal);
    assert(encoded_with_terminal.node != NULL);
    printf("   Prefix encoding with terminal successful: [1,0,1,1] -> integer 42\n");
    
    // 清理
    mtpndd_deref(encoded);
    mtpndd_deref(encoded_with_terminal);
    mtpndd_terminal_deref(terminal);
    
    mtpndd_quit();
    printf("Basic encoding test completed\n\n");
}

void test_bdd_conversion() {
    printf("Test BDD conversion operations\n");
    
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
    
    printf("1. Testing BDD to MTPNDD conversion...\n");
    uint32_t field = ndd_declare_field(2);
    
    // 测试true BDD
    mtpndd_t from_true = mtpndd_from_bdd(ndd_sylvan_true, field);
    assert(from_true.node != NULL);
    assert(mtpndd_is_boolean_terminal(from_true));
    assert(mtpndd_get_boolean(from_true) == true);
    printf("   true BDD conversion successful\n");
    
    // 测试false BDD
    mtpndd_t from_false = mtpndd_from_bdd(ndd_sylvan_false, field);
    assert(from_false.node != NULL);
    assert(mtpndd_is_boolean_terminal(from_false));
    assert(mtpndd_get_boolean(from_false) == false);
    printf("   false BDD conversion successful\n");
    
    printf("2. Testing MTPNDD to BDD conversion...\n");
    
    // 测试布尔终端值转换
    ndd_bdd_t to_true = mtpndd_to_bdd(from_true);
    assert(to_true == ndd_sylvan_true);
    printf("   Boolean true terminal to BDD successful\n");
    
    ndd_bdd_t to_false = mtpndd_to_bdd(from_false);
    assert(to_false == ndd_sylvan_false);
    printf("   Boolean false terminal to BDD successful\n");
    
    // 清理
    mtpndd_deref(from_true);
    mtpndd_deref(from_false);
    
    mtpndd_quit();
    printf("BDD conversion test completed\n\n");
}

void test_value_encoding() {
    printf("Test value encoding operations\n");
    
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
    
    uint32_t field = ndd_declare_field(4);
    
    printf("1. Testing integer range encoding...\n");
    mtpndd_t int_range = mtpndd_encode_integer_range(field, 10, 15);
    assert(int_range.node != NULL);
    assert(mtpndd_get_edge_count(int_range) > 0);
    printf("   Integer range encoding successful: [10, 15]\n");
    
    printf("2. Testing string set encoding...\n");
    const char *strings[] = {"hello", "world", "test"};
    mtpndd_t string_set = mtpndd_encode_string_set(field, strings, 3);
    assert(string_set.node != NULL);
    assert(mtpndd_get_edge_count(string_set) == 3);
    printf("   String set encoding successful: [hello, world, test]\n");
    
    // 清理
    mtpndd_deref(int_range);
    mtpndd_deref(string_set);
    
    mtpndd_quit();
    printf("Value encoding test completed\n\n");
}

void test_value_extraction() {
    printf("Test value extraction operations\n");
    
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
    
    printf("1. Testing integer value extraction...\n");
    mtpndd_t int_node = mtpndd_create_integer_node(42);
    assert(int_node.node != NULL);
    
    mtpndd_value_set_t *int_values = mtpndd_extract_values(int_node, MTPNDD_TERMINAL_INTEGER);
    assert(int_values != NULL);
    assert(int_values->count == 1);
    assert(int_values->values.integers[0] == 42);
    printf("   Integer value extraction successful: %ld\n", int_values->values.integers[0]);
    
    printf("2. Testing string value extraction...\n");
    mtpndd_t string_node = mtpndd_create_string_node("test_string");
    assert(string_node.node != NULL);
    
    mtpndd_value_set_t *string_values = mtpndd_extract_values(string_node, MTPNDD_TERMINAL_STRING);
    assert(string_values != NULL);
    assert(string_values->count == 1);
    assert(strcmp(string_values->values.strings[0], "test_string") == 0);
    printf("   String value extraction successful: %s\n", string_values->values.strings[0]);
    
    // 清理
    mtpndd_deref(int_node);
    mtpndd_deref(string_node);
    mtpndd_value_set_destroy(int_values);
    mtpndd_value_set_destroy(string_values);
    
    mtpndd_quit();
    printf("Value extraction test completed\n\n");
}

int main() {
    printf("=== MTPNDD Encoding/Decoding Test Suite ===\n\n");
    
    test_basic_encoding();
    test_bdd_conversion();
    test_value_encoding();
    test_value_extraction();
    
    printf("All MTPNDD encoding/decoding tests passed!\n");
    printf("\nTest Conclusions:\n");
    printf("- Prefix encoding operations work correctly\n");
    printf("- BDD conversion operations are bidirectional\n");
    printf("- Value encoders are functional\n");
    printf("- Value extractors work properly\n");
    
    printf("\nEncoding/Decoding Features:\n");
    printf("- Prefix encoding supports multi-terminal values\n");
    printf("- BDD bidirectional conversion compatibility\n");
    printf("- Support for integer ranges and string sets\n");
    printf("- Type-safe value extraction operations\n");
    
    return 0;
}