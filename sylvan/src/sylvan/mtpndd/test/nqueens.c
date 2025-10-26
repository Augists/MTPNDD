#include "mtpndd.h"
#include "sylvan.h"
#include "sylvan_table.h"
#include "sylvan_mtbdd.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static inline size_t cell_index(size_t row, size_t col, size_t size) {
    return row * size + col;
}

typedef struct {
    size_t size;
    const mtpndd_field_info_t **fields;
    mtpndd_t **positive;
    mtpndd_t **negative;
    mtpndd_t *formula;
} nqueens_ctx_t;

static inline mtpndd_t *true_node(void) {
    return &MTPNDD_TRUE;
}

static inline mtpndd_t *false_node(void) {
    return &MTPNDD_FALSE;
}

static bool nqueens_ctx_init(nqueens_ctx_t *ctx, size_t size) {
    ctx->size = size;
    ctx->fields = (const mtpndd_field_info_t **)calloc(size, sizeof(*ctx->fields));
    ctx->positive = (mtpndd_t **)calloc(size * size, sizeof(*ctx->positive));
    ctx->negative = (mtpndd_t **)calloc(size * size, sizeof(*ctx->negative));

    if (!ctx->fields || !ctx->positive || !ctx->negative) {
        return false;
    }

    for (size_t row = 0; row < size; ++row) {
        mtpndd_declare_field((uint32_t)size);
        const mtpndd_field_info_t *info = mtpndd_get_field_info((uint32_t)(row + 1));
        if (!info) {
            return false;
        }
        ctx->fields[row] = info;
        for (size_t col = 0; col < size; ++col) {
            size_t idx = cell_index(row, col, size);
            ctx->positive[idx] = mtpndd_get_var((uint32_t)(row + 1), (uint32_t)col);
            ctx->negative[idx] = mtpndd_get_not_var((uint32_t)(row + 1), (uint32_t)col);
            if (!ctx->positive[idx] || !ctx->negative[idx]) {
                return false;
            }
        }
    }

    return true;
}

static void nqueens_ctx_destroy(nqueens_ctx_t *ctx) {
    free(ctx->fields);
    free(ctx->positive);
    free(ctx->negative);
    ctx->fields = NULL;
    ctx->positive = NULL;
    ctx->negative = NULL;
    ctx->formula = NULL;
    ctx->size = 0;
}

static mtpndd_t *build_row_constraint(const nqueens_ctx_t *ctx, size_t row) {
    mtpndd_t *at_least_one = false_node();
    for (size_t col = 0; col < ctx->size; ++col) {
        size_t idx = cell_index(row, col, ctx->size);
        at_least_one = mtpndd_or(at_least_one, ctx->positive[idx]);
        if (!at_least_one) {
            return NULL;
        }
    }

    mtpndd_t *at_most_one = true_node();
    for (size_t col = 0; col < ctx->size; ++col) {
        for (size_t other = col + 1; other < ctx->size; ++other) {
            size_t idx_a = cell_index(row, col, ctx->size);
            size_t idx_b = cell_index(row, other, ctx->size);
            mtpndd_t *pair_ok = mtpndd_or(ctx->negative[idx_a], ctx->negative[idx_b]);
            if (!pair_ok) {
                return NULL;
            }
            at_most_one = mtpndd_and(at_most_one, pair_ok);
            if (!at_most_one) {
                return NULL;
            }
        }
    }

    return mtpndd_and(at_least_one, at_most_one);
}

static mtpndd_t *build_nqueens_formula(nqueens_ctx_t *ctx) {
    mtpndd_t *formula = true_node();

    for (size_t row = 0; row < ctx->size; ++row) {
        mtpndd_t *row_constraint = build_row_constraint(ctx, row);
        if (!row_constraint) {
            return NULL;
        }
        formula = mtpndd_and(formula, row_constraint);
        if (!formula) {
            return NULL;
        }
    }

    for (size_t col = 0; col < ctx->size; ++col) {
        for (size_t row_a = 0; row_a < ctx->size; ++row_a) {
            for (size_t row_b = row_a + 1; row_b < ctx->size; ++row_b) {
                size_t idx_a = cell_index(row_a, col, ctx->size);
                size_t idx_b = cell_index(row_b, col, ctx->size);
                mtpndd_t *no_shared_col = mtpndd_or(ctx->negative[idx_a], ctx->negative[idx_b]);
                if (!no_shared_col) {
                    return NULL;
                }
                formula = mtpndd_and(formula, no_shared_col);
                if (!formula) {
                    return NULL;
                }
            }
        }
    }

    for (size_t row_a = 0; row_a < ctx->size; ++row_a) {
        for (size_t row_b = row_a + 1; row_b < ctx->size; ++row_b) {
            size_t delta = row_b - row_a;
            for (size_t col = 0; col < ctx->size; ++col) {
                size_t diag_col = col + delta;
                if (diag_col < ctx->size) {
                    size_t idx_a = cell_index(row_a, col, ctx->size);
                    size_t idx_b = cell_index(row_b, diag_col, ctx->size);
                    mtpndd_t *no_desc_diag = mtpndd_or(ctx->negative[idx_a], ctx->negative[idx_b]);
                    if (!no_desc_diag) {
                        return NULL;
                    }
                    formula = mtpndd_and(formula, no_desc_diag);
                    if (!formula) {
                        return NULL;
                    }
                }
                if (col >= delta) {
                    size_t diag_col_up = col - delta;
                    size_t idx_a = cell_index(row_a, col, ctx->size);
                    size_t idx_b = cell_index(row_b, diag_col_up, ctx->size);
                    mtpndd_t *no_asc_diag = mtpndd_or(ctx->negative[idx_a], ctx->negative[idx_b]);
                    if (!no_asc_diag) {
                        return NULL;
                    }
                    formula = mtpndd_and(formula, no_asc_diag);
                    if (!formula) {
                        return NULL;
                    }
                }
            }
        }
    }

    ctx->formula = formula;
    return formula;
}

static size_t total_variable_count(const nqueens_ctx_t *ctx) {
    size_t max = 0;
    for (size_t row = 0; row < ctx->size; ++row) {
        const mtpndd_field_info_t *field = ctx->fields[row];
        size_t extent = field->start_var + field->bit_width;
        if (extent > max) {
            max = extent;
        }
    }
    return max;
}

static uint64_t expected_solutions(size_t size) {
    switch (size) {
    case 1:
        return 1;
    case 2:
    case 3:
        return 0;
    case 4:
        return 2;
    case 5:
        return 10;
    case 6:
        return 4;
    case 7:
        return 40;
    case 8:
        return 92;
    case 9:
        return 352;
    case 10:
        return 724;
    case 11:
        return 2680;
    case 12:
        return 14200;
    default:
        return UINT64_MAX;
    }
}

typedef struct {
    size_t size;
    uint64_t expected;
    uint64_t solutions;
    double seconds;
    uint64_t mtpndd_nodes;
    uint64_t mtpndd_edges;
    uint64_t cache_hits;
    uint64_t cache_misses;
    size_t sylvan_nodes;
    size_t sylvan_table_filled;
    size_t sylvan_table_total;
} nqueens_metrics_t;

static double timespec_to_seconds(const struct timespec *start, const struct timespec *end) {
    time_t sec_diff = end->tv_sec - start->tv_sec;
    long nsec_diff = end->tv_nsec - start->tv_nsec;
    return (double)sec_diff + (double)nsec_diff / 1e9;
}

static bool run_case(size_t size, nqueens_metrics_t *metrics) {
    mtpndd_pal_config_t config = {
        .n_workers = 1,
        .lace_dqsize = 1024,
        .bdd_nodetable_size = (size <= 8) ? (1 << 17) : (1 << 19),
        .mtpndd_nodetable_size = (size <= 8) ? (1 << 16) : (1 << 18),
        .op_cache_size = (size <= 8) ? (1 << 17) : (1 << 19),
        .quick_growth_threshold = 0.1,
    };

    if (mtpndd_init(&config) != MTPNDD_SUCCESS) {
        fprintf(stderr, "mtpndd_init failed for size %zu: %s\n", size,
                mtpndd_error_string(mtpndd_get_last_error().code));
        return false;
    }

    bool ok = false;
    nqueens_ctx_t ctx = {0};

    if (!nqueens_ctx_init(&ctx, size)) {
        fprintf(stderr, "Failed to initialise context for size %zu.\n", size);
        goto cleanup;
    }

    struct timespec start = {0}, finish = {0};
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (!build_nqueens_formula(&ctx)) {
        fprintf(stderr, "Failed to build formula for size %zu.\n", size);
        goto cleanup;
    }

    double satcount_value = mtpndd_satcount(ctx.formula);

    clock_gettime(CLOCK_MONOTONIC, &finish);
    double elapsed = timespec_to_seconds(&start, &finish);

    uint64_t solutions = (uint64_t)llround(satcount_value);
    uint64_t expected = expected_solutions(size);
    if (expected != UINT64_MAX && solutions != expected) {
        fprintf(stderr, "Unexpected solution count for size %zu: got %" PRIu64 ", expected %" PRIu64 ".\n",
                size, solutions, expected);
        goto cleanup;
    }

    mtpndd_bdd_t bdd_stats = sylvan_false;
    if (mtpndd_to_mtbdd(ctx.formula, &bdd_stats) != MTPNDD_SUCCESS) {
        fprintf(stderr, "mtpndd_to_mtbdd failed for size %zu: %s\n", size,
                mtpndd_error_string(mtpndd_get_last_error().code));
        goto cleanup;
    }

    size_t sylvan_nodes = sylvan_nodecount(bdd_stats);
    double satcount_verify = mtbdd_satcount(bdd_stats, total_variable_count(&ctx));
    uint64_t satcount_check = (uint64_t)llround(satcount_verify);
    sylvan_deref(bdd_stats);

    if (satcount_check != solutions) {
        fprintf(stderr, "Satcount mismatch for size %zu: API %" PRIu64 ", direct %" PRIu64 ".\n",
                size, solutions, satcount_check);
        goto cleanup;
    }

    size_t table_filled = 0;
    size_t table_total = 0;
    sylvan_table_usage(&table_filled, &table_total);

    if (metrics) {
        metrics->size = size;
        metrics->expected = expected;
        metrics->solutions = solutions;
        metrics->seconds = elapsed;
        metrics->mtpndd_nodes = g_mtpndd_stats.node_count;
        metrics->mtpndd_edges = g_mtpndd_stats.edge_count;
        metrics->cache_hits = g_mtpndd_stats.cache_hits;
        metrics->cache_misses = g_mtpndd_stats.cache_misses;
        metrics->sylvan_nodes = sylvan_nodes;
        metrics->sylvan_table_filled = table_filled;
        metrics->sylvan_table_total = table_total;
    }

    ok = true;

cleanup:
    nqueens_ctx_destroy(&ctx);
    if (mtpndd_quit() != MTPNDD_SUCCESS) {
        fprintf(stderr, "mtpndd_quit reported an error for size %zu.\n", size);
        ok = false;
    }
    return ok;
}

int main(void) {
    const size_t n_min = 3;
    const size_t n_max = 12;
    const size_t total_runs = n_max - n_min + 1;
    nqueens_metrics_t metrics[total_runs];

    size_t recorded = 0;
    for (size_t n = n_min; n <= n_max; ++n) {
        if (!run_case(n, &metrics[recorded])) {
            return EXIT_FAILURE;
        }
        recorded++;
    }

    printf("N-Queens results (n = %zu..%zu)\n", n_min, n_max);
    printf(" n  solutions  expected   time(s)  MTPNDD(nodes/edges)  cache(h/m,hit%%)  Sylvan(nodes)  table(filled/total)\n");
    for (size_t i = 0; i < recorded; ++i) {
        const nqueens_metrics_t *m = &metrics[i];
        double cache_ratio = (m->cache_hits + m->cache_misses) ?
                (double)m->cache_hits / (double)(m->cache_hits + m->cache_misses) * 100.0 : 0.0;
        double table_ratio = m->sylvan_table_total ?
                (double)m->sylvan_table_filled / (double)m->sylvan_table_total * 100.0 : 0.0;
        char expected_buf[32];
        if (m->expected == UINT64_MAX) {
            expected_buf[0] = '-';
            expected_buf[1] = '\0';
        } else {
            snprintf(expected_buf, sizeof(expected_buf), "%" PRIu64, m->expected);
        }
        printf("%2zu %10" PRIu64 " %10s %8.3f  %10" PRIu64 "/%-10" PRIu64 "  %10" PRIu64 "/%-10" PRIu64 " (%.1f%%) %12zu  %8zu/%-8zu (%.1f%%)\n",
               m->size,
               m->solutions,
               expected_buf,
               m->seconds,
               m->mtpndd_nodes,
               m->mtpndd_edges,
               m->cache_hits,
               m->cache_misses,
               cache_ratio,
               m->sylvan_nodes,
               m->sylvan_table_filled,
               m->sylvan_table_total,
               table_ratio);
        if (m->expected != UINT64_MAX && m->solutions != m->expected) {
            fprintf(stderr, "Mismatch detected for n=%zu.\n", m->size);
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
