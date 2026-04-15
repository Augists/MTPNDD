// MTPNDD fraction plus/times micro-benchmark.
//
// Builds two single-field MTPNDDs, each with N distinct-value cubes on the
// field.  Runs a cold plus and then a cold times, timing each operation.
// Invoke with an optional worker count:
//
//     mtpndd_fraction_benchmark [workers] [log2_cubes]
//
// Defaults: workers = 0 (auto), log2_cubes = 6 (64 cubes per operand).

#include "mtpndd.h"
#include "mtpndd_memory_pool.h"
#include "sylvan.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void assert_success(mtpndd_error_t err) {
    if (err != MTPNDD_SUCCESS) {
        fprintf(stderr, "MTPNDD error %d (%s)\n", err, mtpndd_error_string(err));
        abort();
    }
}

static double seconds_since(struct timespec start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start.tv_sec) + 1e-9 * (double)(now.tv_nsec - start.tv_nsec);
}

static mtpndd_bdd_t bit_cube(uint32_t field_id, uint32_t bit_width, uint32_t value) {
    mtpndd_bdd_t acc = sylvan_ref(sylvan_true);
    for (uint32_t bit = 0; bit < bit_width; ++bit) {
        mtpndd_bdd_t lit = (value & (1u << bit))
                ? sylvan_ref(mtpndd_get_bdd_var(field_id, bit))
                : sylvan_ref(mtpndd_get_bdd_not_var(field_id, bit));
        mtpndd_bdd_t next = sylvan_ref(sylvan_and(acc, lit));
        sylvan_deref(acc);
        sylvan_deref(lit);
        acc = next;
    }
    return acc;
}

// Build a 2-field MTPNDD: field 1 has n_cubes cubes, each pointing to a
// distinct field-2 sub-DD that itself has n_cubes cubes mapping to distinct
// double leaves.  Total shape: n_cubes * n_cubes leaves.
// Uses double leaves to avoid int32 overflow in `times` at large k (a
// fraction benchmark of the same shape would need |numer|*|denom| to
// stay inside 2^31, which fails at n_cubes >= ~256).
static mtpndd_t *build_two_field(uint32_t bit_width,
                                 uint32_t n_cubes, double offset) {
    mtpndd_edge_t *top_edges = mtpndd_memory_acquire_edge_map();
    mtpndd_edge_map_init(top_edges);
    double scale = 1.0 / (double)(n_cubes * n_cubes);

    for (uint32_t i = 0; i < n_cubes; ++i) {
        mtpndd_edge_t *sub = mtpndd_memory_acquire_edge_map();
        mtpndd_edge_map_init(sub);
        for (uint32_t j = 0; j < n_cubes; ++j) {
            double value = offset + ((double)(i * n_cubes + j) + 1.0) * scale;
            mtpndd_t *leaf = mtpndd_make_double(value);
            assert(leaf);
            mtpndd_bdd_t cube2 = bit_cube(2, bit_width, j);
            assert_success(mtpndd_add_edge(sub, leaf, cube2));
        }
        mtpndd_t *sub_node = NULL;
        mtpndd_mk(2, sub, &sub_node);
        if (!sub_node) { mtpndd_edge_map_free(sub); abort(); }
        mtpndd_bdd_t cube1 = bit_cube(1, bit_width, i);
        assert_success(mtpndd_add_edge(top_edges, sub_node, cube1));
    }

    mtpndd_t *res = NULL;
    mtpndd_mk(1, top_edges, &res);
    if (!res) { mtpndd_edge_map_free(top_edges); abort(); }
    return res;
}

int main(int argc, char **argv) {
    int workers = (argc > 1) ? atoi(argv[1]) : 0;   // 0 = auto
    int log2_cubes = (argc > 2) ? atoi(argv[2]) : 6;
    if (log2_cubes < 1 || log2_cubes > 12) {
        fprintf(stderr, "log2_cubes out of range (expected 1..12)\n");
        return 1;
    }
    uint32_t n_cubes = 1u << log2_cubes;

    mtpndd_pal_config_t cfg = {
        .n_workers = workers,
        .lace_dqsize = 1 << 20,
        .bdd_nodetable_size = 1 << 18,
        .mtpndd_nodetable_size = 1 << 18,
        .op_cache_size = 1 << 18,
        .edge_bucket_count = 64,
        .nodetable_bucket_count = 1 << 14,
        .node_slab_capacity = 4096,
        .edge_entry_slab_capacity = 16384,
        .nodetable_entry_slab_capacity = 8192,
        .edge_map_slab_capacity = 4096,
    };
    assert_success(mtpndd_init(&cfg));
    assert_success(mtpndd_declare_field((uint32_t)log2_cubes));
    assert_success(mtpndd_declare_field((uint32_t)log2_cubes));
    assert_success(mtpndd_generate_fields());

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    mtpndd_t *a = build_two_field(log2_cubes, n_cubes, 0.0);
    mtpndd_t *b = build_two_field(log2_cubes, n_cubes, 100.0);
    mtpndd_ref(a); mtpndd_ref(b);
    double t_build = seconds_since(t0);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    mtpndd_t *sum = mtpndd_plus(a, b);
    double t_plus = seconds_since(t0);
    assert(sum);
    mtpndd_ref(sum);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    mtpndd_t *prod = mtpndd_times(a, b);
    double t_times = seconds_since(t0);
    assert(prod);

    size_t lc_sum = mtpndd_leafcount(sum);
    size_t lc_prod = mtpndd_leafcount(prod);

    printf("workers=%d  cubes=%u  build=%.3fs  plus=%.3fs  times=%.3fs  leaves(sum=%zu, prod=%zu)\n",
           workers, n_cubes, t_build, t_plus, t_times, lc_sum, lc_prod);

    mtpndd_deref(a); mtpndd_deref(b); mtpndd_deref(sum);
    assert_success(mtpndd_quit());
    return 0;
}
