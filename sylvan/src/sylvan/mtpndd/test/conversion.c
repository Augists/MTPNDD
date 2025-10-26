#include "mtpndd.h"
#include "sylvan.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void assert_success(mtpndd_error_t err) {
    if (err != MTPNDD_SUCCESS) {
        mtpndd_error_info_t info = mtpndd_get_last_error();
        fprintf(stderr, "MTPNDD error %d (%s) at %s:%d\n",
                err, mtpndd_error_string(err), info.function ? info.function : "?", info.line);
        assert(false);
    }
}

static mtpndd_t *checked_node(mtpndd_t *node) {
    if (!node) {
        mtpndd_error_info_t info = mtpndd_get_last_error();
        fprintf(stderr, "MTPNDD op failed (%s) at %s:%d\n",
                mtpndd_error_string(info.code), info.function ? info.function : "?", info.line);
        abort();
    }
    return node;
}

// 生成合法的 IPv4 地址，每个 octet 取值 0-255
static uint32_t generate_random_ip(void) {
    uint32_t ip = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t octet = (uint8_t)(rand() % 256);
        ip |= ((uint32_t)octet) << (8 * i);
    }
    return ip;
}

static void print_ip(uint32_t ip) {
    printf("%u.%u.%u.%u",
           (ip >> 24) & 0xFFu,
           (ip >> 16) & 0xFFu,
           (ip >> 8) & 0xFFu,
           ip & 0xFFu);
}

static mtpndd_bdd_t build_octet_bdd(uint32_t field_id, uint8_t octet) {
    // 对单个 8bit 字段生成精确匹配的 BDD
    mtpndd_bdd_t acc = sylvan_ref(sylvan_true);
    for (uint32_t bit = 0; bit < 8; ++bit) {
        mtpndd_bdd_t literal = (octet & (1u << bit))
                ? sylvan_ref(mtpndd_get_bdd_var(field_id, bit))
                : sylvan_ref(mtpndd_get_bdd_not_var(field_id, bit));
        mtpndd_bdd_t next = sylvan_ref(sylvan_and(acc, literal));
        sylvan_deref(acc);
        sylvan_deref(literal);
        acc = next;
    }
    return acc;
}

static mtpndd_bdd_t build_ip_exact_bdd(const uint32_t field_ids[4], uint32_t ip) {
    // 将 32 位 IP 拆成 4 个 8bit 字段并组合成一个完整的 BDD
    mtpndd_bdd_t acc = sylvan_ref(sylvan_true);
    for (int i = 0; i < 4; ++i) {
        uint8_t octet = (ip >> (8 * i)) & 0xFFu;
        mtpndd_bdd_t oct_bdd = build_octet_bdd(field_ids[i], octet);
        mtpndd_bdd_t next = sylvan_ref(sylvan_and(acc, oct_bdd));
        sylvan_deref(acc);
        sylvan_deref(oct_bdd);
        acc = next;
    }
    return acc;
}

// TODO: 1. check parallel
// TODO: 2. check multi-terminal

int main(void) {
    // 初始化 MTPNDD 运行时
    mtpndd_pal_config_t config = {
        .n_workers = 1,
        .lace_dqsize = 1 << 18,
        .bdd_nodetable_size = 1 << 16,
        .mtpndd_nodetable_size = 1 << 14,
        .op_cache_size = 1 << 12,
    };
    assert_success(mtpndd_init(&config));
    printf(">> mtpndd_init succeeded\n");

    // 为 IP 的四个 octet 分别声明 8bit 字段
    uint32_t ip_field_ids[4];
    for (int i = 0; i < 4; ++i) {
        assert_success(mtpndd_declare_field(8));
        ip_field_ids[i] = (uint32_t)(i + 1);
    }

    srand(42);
    uint32_t ip_a = generate_random_ip();
    uint32_t ip_b = generate_random_ip();
    printf(">> sample IP A: ");
    print_ip(ip_a);
    printf("\n");
    printf(">> sample IP B: ");
    print_ip(ip_b);
    printf("\n");

    mtpndd_bdd_t ip_a_bdd = build_ip_exact_bdd(ip_field_ids, ip_a);
    mtpndd_bdd_t ip_b_bdd = build_ip_exact_bdd(ip_field_ids, ip_b);

    mtpndd_t *ip_a_node = NULL;
    mtpndd_t *ip_b_node = NULL;
    assert_success(mtbdd_to_mtpndd(ip_a_bdd, &ip_a_node));
    assert_success(mtbdd_to_mtpndd(ip_b_bdd, &ip_b_node));
    // TODO: 需要实现mtpndd的printDot来验证mtpndd结果的正确性

    mtpndd_bdd_t ip_a_back = sylvan_false;
    mtpndd_bdd_t ip_b_back = sylvan_false;
    assert_success(mtpndd_to_mtbdd(ip_a_node, &ip_a_back));
    assert_success(mtpndd_to_mtbdd(ip_b_node, &ip_b_back));
    assert(ip_a_back == ip_a_bdd);
    assert(ip_b_back == ip_b_bdd);
    sylvan_deref(ip_a_back);
    sylvan_deref(ip_b_back);
    printf(">> single IP roundtrip ok\n");

    mtpndd_bdd_t expected_union = sylvan_ref(sylvan_or(ip_a_bdd, ip_b_bdd));
    mtpndd_bdd_t expected_intersection = sylvan_ref(sylvan_and(ip_a_bdd, ip_b_bdd));
    mtpndd_bdd_t not_ip_b = sylvan_ref(sylvan_not(ip_b_bdd));
    mtpndd_bdd_t expected_difference = sylvan_ref(sylvan_and(ip_a_bdd, not_ip_b));
    sylvan_deref(not_ip_b);

    mtpndd_t *union_node = checked_node(mtpndd_or(ip_a_node, ip_b_node));
    mtpndd_t *intersection_node = checked_node(mtpndd_and(ip_a_node, ip_b_node));
    mtpndd_t *not_ip_b_node = checked_node(mtpndd_not(ip_b_node));
    mtpndd_t *difference_node = checked_node(mtpndd_and(ip_a_node, not_ip_b_node));

    mtpndd_bdd_t union_back = sylvan_false;
    mtpndd_bdd_t intersection_back = sylvan_false;
    mtpndd_bdd_t difference_back = sylvan_false;
    assert_success(mtpndd_to_mtbdd(union_node, &union_back));
    assert_success(mtpndd_to_mtbdd(intersection_node, &intersection_back));
    assert_success(mtpndd_to_mtbdd(difference_node, &difference_back));
    assert(union_back == expected_union);
    assert(intersection_back == expected_intersection);
    assert(difference_back == expected_difference);
    sylvan_deref(union_back);
    sylvan_deref(intersection_back);
    sylvan_deref(difference_back);
    printf(">> union/intersection/difference ok\n");

    sylvan_deref(expected_union);
    sylvan_deref(expected_intersection);
    sylvan_deref(expected_difference);
    sylvan_deref(ip_a_bdd);
    sylvan_deref(ip_b_bdd);

    assert_success(mtpndd_quit());
    printf(">> mtpndd_quit succeeded\n");
    return 0;
}
