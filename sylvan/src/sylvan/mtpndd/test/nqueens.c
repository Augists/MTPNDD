#include "mtpndd.h"
#include "mtpndd_common.h"
#include "mtpndd_node.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    size_t size;
    uint32_t bit_width;
    uint32_t *field_ids;
} nqueens_ctx_t;

typedef struct {
    size_t size;
    uint64_t expected;
    uint64_t solutions;
    double seconds;
    uint64_t mtpndd_nodes;
#ifdef ENABLE_RECORDING
    uint64_t mtpndd_nodes_created;
    uint64_t mtpndd_nodes_reused;
    uint64_t mtpndd_nodes_collected;
    uint64_t mtpndd_max_edges;
    uint64_t cache_hits;
    uint64_t cache_misses;
#endif
} nqueens_metrics_t;

static uint32_t ceil_log2_uint(uint32_t value) {
    if (value <= 1) return 1;
#if defined(__GNUC__)
    return 32u - (uint32_t)__builtin_clz(value - 1);
#else
    uint32_t bits = 0;
    uint32_t v = value - 1;
    while (v) {
        v >>= 1u;
        bits++;
    }
    return bits;
#endif
}

static mtpndd_t *mtpndd_hold(mtpndd_t *node) {
    if (!node) return NULL;
    if (mtpndd_ref(node) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return node;
}

static mtpndd_t *mtpndd_not_hold(mtpndd_t *node) {
    mtpndd_t *result = mtpndd_not(node);
    if (!result) return NULL;
    if (mtpndd_ref(result) != MTPNDD_SUCCESS) {
        mtpndd_deref(result);
        return NULL;
    }
    return result;
}

static mtpndd_t *mtpndd_and_release(mtpndd_t *left, mtpndd_t *right) {
    mtpndd_t *result = mtpndd_and(left, right);
    mtpndd_deref(left);
    mtpndd_deref(right);
    if (!result) return NULL;
    if (mtpndd_ref(result) != MTPNDD_SUCCESS) {
        mtpndd_deref(result);
        return NULL;
    }
    return result;
}

static mtpndd_t *mtpndd_or_release(mtpndd_t *left, mtpndd_t *right) {
    mtpndd_t *result = mtpndd_or(left, right);
    mtpndd_deref(left);
    mtpndd_deref(right);
    if (!result) return NULL;
    if (mtpndd_ref(result) != MTPNDD_SUCCESS) {
        mtpndd_deref(result);
        return NULL;
    }
    return result;
}

static bool nqueens_ctx_init(nqueens_ctx_t *ctx, size_t size) {
    ctx->size = size;
    ctx->bit_width = ceil_log2_uint((uint32_t)size);
    ctx->field_ids = (uint32_t *)calloc(size, sizeof(uint32_t));
    if (!ctx->field_ids) return false;

    for (size_t row = 0; row < size; ++row) {
        if (mtpndd_declare_field(ctx->bit_width) != MTPNDD_SUCCESS) {
            return false;
        }
        ctx->field_ids[row] = (uint32_t)(row + 1);
    }

    return true;
}

static void nqueens_ctx_destroy(nqueens_ctx_t *ctx) {
    free(ctx->field_ids);
    ctx->field_ids = NULL;
    ctx->size = 0;
    ctx->bit_width = 0;
}

static mtpndd_t *build_value_equals(const nqueens_ctx_t *ctx, size_t row, uint32_t value) {
    mtpndd_t *result = mtpndd_hold(&MTPNDD_TRUE);
    if (!result) return NULL;
    uint32_t field_id = ctx->field_ids[row];
    for (uint32_t bit = 0; bit < ctx->bit_width; ++bit) {
        bool bit_set = ((value >> bit) & 1u) != 0;
        mtpndd_t *literal = bit_set
                ? mtpndd_hold(mtpndd_get_var(field_id, bit))
                : mtpndd_hold(mtpndd_get_not_var(field_id, bit));
        if (!literal) {
            mtpndd_deref(result);
            return NULL;
        }
        result = mtpndd_and_release(result, literal);
        if (!result) return NULL;
    }
    return result;
}

static mtpndd_t *build_row_domain(const nqueens_ctx_t *ctx, size_t row) {
    uint32_t limit = 1u << ctx->bit_width;
    if (limit == ctx->size) {
        return mtpndd_hold(&MTPNDD_TRUE);
    }

    mtpndd_t *domain = mtpndd_hold(&MTPNDD_TRUE);
    if (!domain) return NULL;
    for (uint32_t value = (uint32_t)ctx->size; value < limit; ++value) {
        mtpndd_t *eq = build_value_equals(ctx, row, value);
        if (!eq) {
            mtpndd_deref(domain);
            return NULL;
        }
        mtpndd_t *not_eq = mtpndd_not_hold(eq);
        mtpndd_deref(eq);
        if (!not_eq) {
            mtpndd_deref(domain);
            return NULL;
        }
        domain = mtpndd_and_release(domain, not_eq);
        if (!domain) return NULL;
    }
    return domain;
}

static mtpndd_t *build_row_at_least_one(mtpndd_t **row_values, size_t size) {
    mtpndd_t *accum = mtpndd_hold(&MTPNDD_FALSE);
    if (!accum) return NULL;
    for (size_t col = 0; col < size; ++col) {
        mtpndd_t *candidate = mtpndd_hold(row_values[col]);
        if (!candidate) {
            mtpndd_deref(accum);
            return NULL;
        }
        accum = mtpndd_or_release(accum, candidate);
        if (!accum) return NULL;
    }
    return accum;
}

static mtpndd_t *forbid_pair(mtpndd_t ***eq_cache,
                             size_t row_a, size_t value_a,
                             size_t row_b, size_t value_b) {
    mtpndd_t *left = mtpndd_hold(eq_cache[row_a][value_a]);
    mtpndd_t *right = mtpndd_hold(eq_cache[row_b][value_b]);
    if (!left || !right) {
        if (left) mtpndd_deref(left);
        if (right) mtpndd_deref(right);
        return NULL;
    }
    mtpndd_t *both = mtpndd_and_release(left, right);
    if (!both) return NULL;
    mtpndd_t *clause = mtpndd_not_hold(both);
    mtpndd_deref(both);
    return clause;
}

static void release_eq_cache(mtpndd_t ***cache, size_t size) {
    if (!cache) return;
    for (size_t row = 0; row < size; ++row) {
        if (!cache[row]) continue;
        for (size_t col = 0; col < size; ++col) {
            if (cache[row][col]) {
                mtpndd_deref(cache[row][col]);
                cache[row][col] = NULL;
            }
        }
        free(cache[row]);
    }
    free(cache);
}

static bool build_nqueens_formula(const nqueens_ctx_t *ctx, mtpndd_t **out_formula) {
    size_t n = ctx->size;
    mtpndd_t ***eq_cache = (mtpndd_t ***)calloc(n, sizeof(mtpndd_t **));
    if (!eq_cache) return false;

    bool ok = false;
    mtpndd_t *formula = NULL;

    for (size_t row = 0; row < n; ++row) {
        eq_cache[row] = (mtpndd_t **)calloc(n, sizeof(mtpndd_t *));
        if (!eq_cache[row]) goto cleanup;
        for (size_t value = 0; value < n; ++value) {
            eq_cache[row][value] = build_value_equals(ctx, row, (uint32_t)value);
            if (!eq_cache[row][value]) goto cleanup;
        }
    }

    formula = mtpndd_hold(&MTPNDD_TRUE);
    if (!formula) goto cleanup;

    for (size_t row = 0; row < n; ++row) {
        mtpndd_t *domain = build_row_domain(ctx, row);
        if (!domain) goto cleanup;
        formula = mtpndd_and_release(formula, domain);
        if (!formula) goto cleanup;
    }

    for (size_t row = 0; row < n; ++row) {
        mtpndd_t *at_least_one = build_row_at_least_one(eq_cache[row], n);
        if (!at_least_one) goto cleanup;
        formula = mtpndd_and_release(formula, at_least_one);
        if (!formula) goto cleanup;
    }

    for (size_t a = 0; a < n; ++a) {
        for (size_t b = a + 1; b < n; ++b) {
            for (size_t col = 0; col < n; ++col) {
                mtpndd_t *clause = forbid_pair(eq_cache, a, col, b, col);
                if (!clause) goto cleanup;
                formula = mtpndd_and_release(formula, clause);
                if (!formula) goto cleanup;
            }
            size_t delta = b - a;
            for (size_t col = 0; col < n; ++col) {
                size_t other = col + delta;
                if (other < n) {
                    mtpndd_t *clause = forbid_pair(eq_cache, a, col, b, other);
                    if (!clause) goto cleanup;
                    formula = mtpndd_and_release(formula, clause);
                    if (!formula) goto cleanup;
                }
                if (col >= delta) {
                    other = col - delta;
                    mtpndd_t *clause = forbid_pair(eq_cache, a, col, b, other);
                    if (!clause) goto cleanup;
                    formula = mtpndd_and_release(formula, clause);
                    if (!formula) goto cleanup;
                }
            }
        }
    }

    *out_formula = formula;
    formula = NULL;
    ok = true;

cleanup:
    if (formula) mtpndd_deref(formula);
    release_eq_cache(eq_cache, n);
    return ok;
}

static uint64_t expected_solutions(size_t size) {
    switch (size) {
        case 1: return 1;
        case 2:
        case 3: return 0;
        case 4: return 2;
        case 5: return 10;
        case 6: return 4;
        case 7: return 40;
        case 8: return 92;
        case 9: return 352;
        case 10: return 724;
        case 11: return 2680;
        case 12: return 14200;
        default: return UINT64_MAX;
    }
}

static double timespec_diff_seconds(const struct timespec *start, const struct timespec *end) {
    time_t sec_diff = end->tv_sec - start->tv_sec;
    long nsec_diff = end->tv_nsec - start->tv_nsec;
    return (double)sec_diff + (double)nsec_diff / 1e9;
}

#ifdef ENABLE_RECORDING
static void print_run_stats(const mtpndd_stats_t *stats) {
    if (!stats) return;
    printf(".. stats: created=%" PRIu64 ", reused=%" PRIu64 ", collected=%" PRIu64 "\n",
           stats->nodes_created_total, stats->nodes_reused_total, stats->nodes_collected_last);
    printf(".. stats: max_edges_per_node=%" PRIu64 ", cache hits/misses=%" PRIu64 "/%" PRIu64 "\n",
           stats->max_edges_per_node, stats->cache_lookup_hits, stats->cache_lookup_misses);
}
#endif

static bool run_case(size_t size, nqueens_metrics_t *metrics) {
    printf("== solving n=%zu\n", size);
    fflush(stdout);

    size_t bdd_size = (size <= 7) ? (1 << 19) : (size <= 9) ? (1 << 22) : (1 << 25);
    size_t ndd_size = (size <= 7) ? (1 << 18) : (size <= 9) ? (1 << 20) : (1 << 23);
    size_t cache_size = (size <= 7) ? (1 << 18) : (size <= 9) ? (1 << 20) : (1 << 23);
    size_t edge_bucket_count = (size <= 7) ? 32 : (size <= 9) ? 128 : 512;
    size_t nodetable_bucket_count = ndd_size;
    size_t gc_bucket_count = (size <= 7) ? 512 : (size <= 9) ? 2048 : 8192;
    size_t node_slab_capacity = (size <= 7) ? 1024 : (size <= 9) ? 2048 : 4096;
    size_t edge_entry_slab_capacity = (size <= 7) ? 2048 : (size <= 9) ? 4096 : 8192;
    size_t nodetable_entry_slab_capacity = (size <= 7) ? 1024 : (size <= 9) ? 2048 : 4096;
    size_t edge_map_slab_capacity = (size <= 7) ? 512 : (size <= 9) ? 1024 : 2048;

    if (size >= 10) {
        bdd_size <<= 1;               // double BDD table to avoid Sylvan pressure
        ndd_size <<= 2;               // quadruple MTPNDD node table
        cache_size <<= 1;
        edge_bucket_count *= 4;
        nodetable_bucket_count = ndd_size;
        gc_bucket_count *= 2;
        node_slab_capacity <<= 1;
        edge_entry_slab_capacity <<= 1;
        nodetable_entry_slab_capacity <<= 1;
        edge_map_slab_capacity <<= 1;
    }

    mtpndd_pal_config_t config = {
        .n_workers = 0,
        .lace_dqsize = 1024,
        .bdd_nodetable_size = bdd_size,
        .mtpndd_nodetable_size = ndd_size,
        .op_cache_size = cache_size,
        .quick_growth_threshold = 0.1,
        .edge_bucket_count = edge_bucket_count,
        .nodetable_bucket_count = nodetable_bucket_count,
        .gc_bucket_count = gc_bucket_count,
        .node_slab_capacity = node_slab_capacity,
        .edge_entry_slab_capacity = edge_entry_slab_capacity,
        .nodetable_entry_slab_capacity = nodetable_entry_slab_capacity,
        .edge_map_slab_capacity = edge_map_slab_capacity,
    };

    if (mtpndd_init(&config) != MTPNDD_SUCCESS) {
        fprintf(stderr, "mtpndd_init failed for size %zu: %s\n", size,
                mtpndd_error_string(mtpndd_get_last_error().code));
        return false;
    }

    bool ok = false;
    nqueens_ctx_t ctx = {0};
    mtpndd_t *formula = NULL;

    if (!nqueens_ctx_init(&ctx, size)) {
        fprintf(stderr, "Failed to initialise context for size %zu.\n", size);
        goto cleanup;
    }

    printf(".. building formula\n");
    fflush(stdout);
    if (!build_nqueens_formula(&ctx, &formula)) {
        fprintf(stderr, "Failed to build formula for size %zu.\n", size);
        goto cleanup;
    }
    printf(".. formula built\n");
    fflush(stdout);

    struct timespec start = {0}, finish = {0};
    clock_gettime(CLOCK_MONOTONIC, &start);
    printf(".. running mtpndd_satcount\n");
    fflush(stdout);
    double satcount_value = mtpndd_satcount(formula);
    clock_gettime(CLOCK_MONOTONIC, &finish);
    printf(".. satcount done\n");
    fflush(stdout);

    double elapsed = timespec_diff_seconds(&start, &finish);
    uint64_t solutions = (uint64_t)llround(satcount_value);
    uint64_t expected = expected_solutions(size);
    if (expected != UINT64_MAX && solutions != expected) {
        fprintf(stderr, "Unexpected solution count for size %zu: got %" PRIu64 ", expected %" PRIu64 ".\n",
                size, solutions, expected);
        goto cleanup;
    }

    const mtpndd_stats_t *stats = mtpndd_get_stats();
#ifdef ENABLE_RECORDING
    print_run_stats(stats);
#endif

    if (metrics) {
        metrics->size = size;
        metrics->expected = expected;
        metrics->solutions = solutions;
        metrics->seconds = elapsed;
        metrics->mtpndd_nodes = stats ? stats->node_count : 0;
#ifdef ENABLE_RECORDING
        if (stats) {
            metrics->mtpndd_nodes_created = stats->nodes_created_total;
            metrics->mtpndd_nodes_reused = stats->nodes_reused_total;
            metrics->mtpndd_nodes_collected = stats->nodes_collected_last;
            metrics->mtpndd_max_edges = stats->max_edges_per_node;
            metrics->cache_hits = stats->cache_lookup_hits;
            metrics->cache_misses = stats->cache_lookup_misses;
        } else {
            metrics->mtpndd_nodes_created = 0;
            metrics->mtpndd_nodes_reused = 0;
            metrics->mtpndd_nodes_collected = 0;
            metrics->mtpndd_max_edges = 0;
            metrics->cache_hits = 0;
            metrics->cache_misses = 0;
        }
#endif
    }

    ok = true;

cleanup:
    if (formula) {
        mtpndd_deref(formula);
        formula = NULL;
    }
    nqueens_ctx_destroy(&ctx);
    if (mtpndd_quit() != MTPNDD_SUCCESS) {
        fprintf(stderr, "mtpndd_quit reported an error for size %zu.\n", size);
        ok = false;
    }
    return ok;
}

int main(void) {
    const size_t n_min = 8;
    const size_t n_max = 8;
    const size_t total_runs = n_max - n_min + 1;
    nqueens_metrics_t metrics[total_runs];

    size_t recorded = 0;
    for (size_t n = n_min; n <= n_max; ++n) {
        if (!run_case(n, &metrics[recorded])) {
            return EXIT_FAILURE;
        }
        printf("== result n=%zu -> solutions=%" PRIu64 ", time=%.3f s\n",
               metrics[recorded].size,
               metrics[recorded].solutions,
               metrics[recorded].seconds);
        fflush(stdout);
        recorded++;
    }

    printf("N-Queens results (n = %zu..%zu)\n", n_min, n_max);
#ifdef ENABLE_RECORDING
    printf(" n  solutions  expected   time(s)  nodes(created/reused/collected)  maxEdges  cache(h/m)\n");
#else
    printf(" n  solutions  expected   time(s)  MTPNDD(nodes)\n");
#endif
    for (size_t i = 0; i < recorded; ++i) {
        const nqueens_metrics_t *m = &metrics[i];
        char expected_buf[32];
        if (m->expected == UINT64_MAX) {
            expected_buf[0] = '-';
            expected_buf[1] = '\0';
        } else {
            snprintf(expected_buf, sizeof(expected_buf), "%" PRIu64, m->expected);
        }
#ifdef ENABLE_RECORDING
        printf("%2zu %10" PRIu64 " %10s %8.3f  %10" PRIu64 " (%" PRIu64 "/%" PRIu64 "/%" PRIu64 ")  %7" PRIu64
               "  %10" PRIu64 "/%-10" PRIu64 "\n",
               m->size,
               m->solutions,
               expected_buf,
               m->seconds,
               m->mtpndd_nodes,
               m->mtpndd_nodes_created,
               m->mtpndd_nodes_reused,
               m->mtpndd_nodes_collected,
               m->mtpndd_max_edges,
               m->cache_hits,
               m->cache_misses);
#else
        printf("%2zu %10" PRIu64 " %10s %8.3f  %10" PRIu64 "\n",
               m->size,
               m->solutions,
               expected_buf,
               m->seconds,
               m->mtpndd_nodes);
#endif
        if (m->expected != UINT64_MAX && m->solutions != m->expected) {
            fprintf(stderr, "Mismatch detected for n=%zu.\n", m->size);
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
