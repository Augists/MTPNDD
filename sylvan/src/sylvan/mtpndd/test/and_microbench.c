// Microbenchmarks for mtpndd_and scheduling strategies.
// Focus: same-field AND with controllable edge counts and recursion depth.

#include "mtpndd_common.h"
#include "mtpndd_memory_pool.h"
#include "mtpndd_nodetable.h"
#include "mtpndd_node.h"

#include "sylvan.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline double timespec_diff_seconds(const struct timespec *start, const struct timespec *end) {
    time_t sec_diff = end->tv_sec - start->tv_sec;
    long nsec_diff = end->tv_nsec - start->tv_nsec;
    return (double)sec_diff + (double)nsec_diff / 1e9;
}

static size_t parse_size(const char *s, const char *name) {
    if (!s || !*s) {
        fprintf(stderr, "missing %s\n", name);
        exit(EXIT_FAILURE);
    }
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (!end || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (size_t)v;
}

static size_t max3(size_t a, size_t b, size_t c) {
    size_t m = a > b ? a : b;
    return m > c ? m : c;
}

static bool declare_fields(uint32_t field_count, uint32_t bit_width) {
    for (uint32_t i = 0; i < field_count; ++i) {
        if (mtpndd_declare_field(bit_width) != MTPNDD_SUCCESS) return false;
    }
    return mtpndd_generate_fields() == MTPNDD_SUCCESS;
}

static mtpndd_t *mk_node_one_edge(uint32_t field, uint32_t var_idx) {
    mtpndd_edge_t *edges = mtpndd_memory_acquire_edge_map();
    if (!edges) return NULL;
    mtpndd_edge_map_init(edges);

    mtpndd_bdd_t label = sylvan_ref(mtpndd_get_bdd_var(field, var_idx));
    if (mtpndd_add_edge(edges, &MTPNDD_TRUE, label) != MTPNDD_SUCCESS) {
        mtpndd_edge_map_free(edges);
        return NULL;
    }

    mtpndd_t *node = NULL;
    mtpndd_mk(field, edges, &node);
    return node;
}

static mtpndd_t *mk_node_fanout(uint32_t field, mtpndd_t **children, size_t child_count,
                                uint32_t bit_width, size_t fanout, size_t i_seed) {
    mtpndd_edge_t *edges = mtpndd_memory_acquire_edge_map();
    if (!edges) return NULL;
    mtpndd_edge_map_init(edges);

    for (size_t j = 0; j < fanout; ++j) {
        size_t child_idx = (i_seed * fanout + j) % child_count;
        mtpndd_t *child = children[child_idx];
        mtpndd_bdd_t label = sylvan_ref(mtpndd_get_bdd_var(field, (uint32_t)((i_seed + j) % bit_width)));
        if (mtpndd_add_edge(edges, child, label) != MTPNDD_SUCCESS) {
            mtpndd_edge_map_free(edges);
            return NULL;
        }
    }

    mtpndd_t *node = NULL;
    mtpndd_mk(field, edges, &node);
    return node;
}

static mtpndd_t *mk_parent(uint32_t field, mtpndd_t **children, size_t n_children) {
    mtpndd_edge_t *edges = mtpndd_memory_acquire_edge_map();
    if (!edges) return NULL;
    mtpndd_edge_map_init(edges);

    for (size_t i = 0; i < n_children; ++i) {
        mtpndd_bdd_t label = sylvan_ref(sylvan_true); // always intersects; stresses scheduling + recursion
        if (mtpndd_add_edge(edges, children[i], label) != MTPNDD_SUCCESS) {
            mtpndd_edge_map_free(edges);
            return NULL;
        }
    }

    mtpndd_t *node = NULL;
    mtpndd_mk(field, edges, &node);
    return node;
}

static int run_bench(const char *mode, size_t na, size_t nb, size_t iters, size_t workers, size_t depth, size_t fanout) {
    const size_t bdd_size = 1u << 20;
    const size_t bdd_cache = 1u << 19;
    const size_t ndd_size = 1u << 20;

    mtpndd_pal_config_t cfg = {
        .n_workers = (int32_t)workers,
        .lace_dqsize = 1 << 20,
        .bdd_nodetable_size = bdd_size,
        .mtpndd_nodetable_size = ndd_size,
        .op_cache_size = bdd_cache,
        .edge_bucket_count = 0,
        .nodetable_bucket_count = 0,
        .node_slab_capacity = 0,
        .edge_entry_slab_capacity = 0,
        .nodetable_entry_slab_capacity = 0,
        .edge_map_slab_capacity = 0,
    };

    if (mtpndd_init(&cfg) != MTPNDD_SUCCESS) {
        fprintf(stderr, "mtpndd_init failed: %s\n", mtpndd_error_string(mtpndd_get_last_error().code));
        return 1;
    }

    uint32_t field_count = (uint32_t)(1 + (depth ? depth : 1));
    uint32_t bit_width = (uint32_t)max3(32, na > nb ? na : nb, fanout);
    if (!declare_fields(field_count, bit_width)) {
        fprintf(stderr, "declare_fields failed\n");
        mtpndd_quit();
        return 1;
    }

    const size_t child_count = na > nb ? na : nb;
    mtpndd_t **level_nodes = (mtpndd_t **)calloc(child_count, sizeof(*level_nodes));
    if (!level_nodes) {
        fprintf(stderr, "allocation failed\n");
        mtpndd_quit();
        return 1;
    }

    // Build deepest level first.
    uint32_t leaf_field = field_count;
    for (size_t i = 0; i < child_count; ++i) {
        level_nodes[i] = mk_node_one_edge(leaf_field, (uint32_t)(i % bit_width));
        if (!level_nodes[i]) {
            fprintf(stderr, "mk_node_one_edge failed\n");
            free(level_nodes);
            mtpndd_quit();
            return 1;
        }
    }

    // Build intermediate levels (if any), reusing the array as the "current level".
    for (uint32_t f = leaf_field; f > 2; --f) {
        uint32_t cur_field = f - 1;
        mtpndd_t **next_level = (mtpndd_t **)calloc(child_count, sizeof(*next_level));
        if (!next_level) {
            fprintf(stderr, "allocation failed\n");
            free(level_nodes);
            mtpndd_quit();
            return 1;
        }
        for (size_t i = 0; i < child_count; ++i) {
            next_level[i] = mk_node_fanout(cur_field, level_nodes, child_count, bit_width, fanout, i);
            if (!next_level[i]) {
                fprintf(stderr, "mk_node_fanout failed\n");
                free(next_level);
                free(level_nodes);
                mtpndd_quit();
                return 1;
            }
        }
        free(level_nodes);
        level_nodes = next_level;
    }

    mtpndd_t **children_a = level_nodes;
    mtpndd_t **children_b = level_nodes;

    mtpndd_t *a = mk_parent(1, children_a, na);
    mtpndd_t *b = mk_parent(1, children_b, nb);
    if (!a || !b) {
        fprintf(stderr, "mk_parent failed\n");
        free(level_nodes);
        mtpndd_quit();
        return 1;
    }

    // Keep roots alive across the benchmark (GC uses ref_count).
    mtpndd_ref(a);
    mtpndd_ref(b);

    printf("mode=%s na=%zu nb=%zu iters=%zu workers=%zu depth=%zu fanout=%zu\n",
           mode, na, nb, iters, workers, depth, fanout);
    printf("env MTPNDD_AND_OUTER_CHUNK=%s\n", getenv("MTPNDD_AND_OUTER_CHUNK") ? getenv("MTPNDD_AND_OUTER_CHUNK") : "(unset)");
    printf("built: a.edges=%zu b.edges=%zu\n",
           a->edges ? a->edges->edge_count : 0, b->edges ? b->edges->edge_count : 0);

#if LACE_COUNT_EVENTS
    lace_count_reset();
#endif

    struct timespec start_ts = {0}, end_ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &start_ts);
    for (size_t i = 0; i < iters; ++i) {
        mtpndd_t *res = mtpndd_and(a, b);
        if (!res) {
            fprintf(stderr, "mtpndd_and failed: %s\n", mtpndd_error_string(mtpndd_get_last_error().code));
            mtpndd_deref(a);
            mtpndd_deref(b);
            free(level_nodes);
            mtpndd_quit();
            return 1;
        }
        mtpndd_ref(res);
        mtpndd_deref(res);
    }
    clock_gettime(CLOCK_MONOTONIC, &end_ts);

    double elapsed = timespec_diff_seconds(&start_ts, &end_ts);
    printf("time_sec=%.6f iter_sec=%.6g\n", elapsed, iters ? elapsed / (double)iters : 0.0);

#if LACE_COUNT_EVENTS
    printf("== lace counters ==\n");
    lace_count_report_file(stdout);
#endif

    mtpndd_deref(a);
    mtpndd_deref(b);
    free(level_nodes);
    mtpndd_quit();
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr,
                "Usage:\n"
                "  %s shallow <na> <nb> <iters> <workers>\n"
                "  %s mixed   <na> <nb> <iters> <workers> <depth> <fanout>\n",
                argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    const char *mode = argv[1];
    size_t na = parse_size(argv[2], "na");
    size_t nb = parse_size(argv[3], "nb");
    size_t iters = parse_size(argv[4], "iters");
    size_t workers = parse_size(argv[5], "workers");

    size_t depth = 1;
    size_t fanout = 1;
    if (strcmp(mode, "shallow") == 0) {
        depth = 1;
        fanout = 1;
    } else if (strcmp(mode, "mixed") == 0) {
        if (argc < 8) {
            fprintf(stderr, "mixed mode requires <depth> <fanout>\n");
            return EXIT_FAILURE;
        }
        depth = parse_size(argv[6], "depth");
        fanout = parse_size(argv[7], "fanout");
        if (depth < 2) depth = 2;
        if (fanout < 1) fanout = 1;
    } else {
        fprintf(stderr, "unknown mode: %s\n", mode);
        return EXIT_FAILURE;
    }

    return run_bench(mode, na, nb, iters, workers, depth, fanout);
}

