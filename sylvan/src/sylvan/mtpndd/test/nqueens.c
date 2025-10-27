#include "mtpndd.h"
#include "sylvan.h"
#include "sylvan_table.h"
#include "sylvan_mtbdd.h"
#include "sylvan_bdd.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

static uint32_t ceil_log2_uint(uint32_t value) {
    if (value <= 1) {
        return 1;
    }
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

typedef struct {
    size_t size;
    const mtpndd_field_info_t **fields;
    mtpndd_t *formula;
} nqueens_ctx_t;

static bool nqueens_ctx_init(nqueens_ctx_t *ctx, size_t size) {
    ctx->size = size;
    ctx->fields = (const mtpndd_field_info_t **)calloc(size, sizeof(*ctx->fields));

    if (!ctx->fields) {
        return false;
    }

    uint32_t bit_width = ceil_log2_uint((uint32_t)size);
    for (size_t row = 0; row < size; ++row) {
        mtpndd_declare_field(bit_width);
        const mtpndd_field_info_t *info = mtpndd_get_field_info((uint32_t)(row + 1));
        if (!info) {
            return false;
        }
        ctx->fields[row] = info;
    }

    return true;
}

static void nqueens_ctx_destroy(nqueens_ctx_t *ctx) {
    free(ctx->fields);
    ctx->fields = NULL;
    ctx->formula = NULL;
    ctx->size = 0;
}

static inline void bdd_and_assign(mtpndd_bdd_t *target, mtpndd_bdd_t constraint) {
    mtpndd_bdd_t next = sylvan_ref(sylvan_and(*target, constraint));
    sylvan_deref(*target);
    sylvan_deref(constraint);
    *target = next;
}

static mtpndd_bdd_t build_value_equals_bdd(const mtpndd_field_info_t *field, uint32_t value) {
    mtpndd_bdd_t result = sylvan_true;
    sylvan_ref(result);
    for (uint32_t bit = 0; bit < field->bit_width; ++bit) {
        mtpndd_bdd_t literal = (value & (1u << bit)) ? field->bdd_vars[bit] : field->bdd_not_vars[bit];
        mtpndd_bdd_t next = sylvan_ref(sylvan_and(result, literal));
        sylvan_deref(result);
        result = next;
    }
    return result;
}

static mtpndd_bdd_t build_row_domain_bdd(const nqueens_ctx_t *ctx, size_t row) {
    const mtpndd_field_info_t *field = ctx->fields[row];
    uint32_t max_value = (uint32_t)ctx->size;
    uint32_t limit = 1u << field->bit_width;

    if (limit == max_value) {
        mtpndd_bdd_t identity = sylvan_true;
        sylvan_ref(identity);
        return identity;
    }

    mtpndd_bdd_t domain = sylvan_true;
    sylvan_ref(domain);
    for (uint32_t value = max_value; value < limit; ++value) {
        mtpndd_bdd_t eq = build_value_equals_bdd(field, value);
        mtpndd_bdd_t not_eq = sylvan_ref(sylvan_not(eq));
        sylvan_deref(eq);
        bdd_and_assign(&domain, not_eq);
    }
    return domain;
}

static mtpndd_bdd_t build_rows_not_equal_bdd(const nqueens_ctx_t *ctx, size_t row_a, size_t row_b) {
    const mtpndd_field_info_t *field_a = ctx->fields[row_a];
    const mtpndd_field_info_t *field_b = ctx->fields[row_b];
    mtpndd_bdd_t eq = sylvan_true;
    sylvan_ref(eq);
    for (uint32_t bit = 0; bit < field_a->bit_width; ++bit) {
        mtpndd_bdd_t same = sylvan_ref(sylvan_not(sylvan_xor(field_a->bdd_vars[bit], field_b->bdd_vars[bit])));
        mtpndd_bdd_t next = sylvan_ref(sylvan_and(eq, same));
        sylvan_deref(eq);
        sylvan_deref(same);
        eq = next;
    }
    mtpndd_bdd_t not_equal = sylvan_ref(sylvan_not(eq));
    sylvan_deref(eq);
    return not_equal;
}

static void forbid_value_pair(mtpndd_bdd_t *accum,
        const mtpndd_field_info_t *field_a, uint32_t value_a,
        const mtpndd_field_info_t *field_b, uint32_t value_b) {
    mtpndd_bdd_t eq_a = build_value_equals_bdd(field_a, value_a);
    mtpndd_bdd_t eq_b = build_value_equals_bdd(field_b, value_b);
    mtpndd_bdd_t both = sylvan_ref(sylvan_and(eq_a, eq_b));
    sylvan_deref(eq_a);
    sylvan_deref(eq_b);
    mtpndd_bdd_t not_both = sylvan_ref(sylvan_not(both));
    sylvan_deref(both);
    bdd_and_assign(accum, not_both);
}

static mtpndd_bdd_t build_diagonal_pair_constraint(const nqueens_ctx_t *ctx, size_t row_a, size_t row_b) {
    const mtpndd_field_info_t *field_a = ctx->fields[row_a];
    const mtpndd_field_info_t *field_b = ctx->fields[row_b];
    mtpndd_bdd_t constraint = sylvan_true;
    sylvan_ref(constraint);
    size_t delta = row_b - row_a;

    for (size_t col = 0; col < ctx->size; ++col) {
        size_t diag_down = col + delta;
        if (diag_down < ctx->size) {
            forbid_value_pair(&constraint, field_a, (uint32_t)col, field_b, (uint32_t)diag_down);
        }
        if (col >= delta) {
            size_t diag_up = col - delta;
            forbid_value_pair(&constraint, field_a, (uint32_t)col, field_b, (uint32_t)diag_up);
        }
    }

    return constraint;
}

static bool build_nqueens_bdd(nqueens_ctx_t *ctx, mtpndd_bdd_t *out_formula) {
    mtpndd_bdd_t formula = sylvan_true;
    sylvan_ref(formula);

    for (size_t row = 0; row < ctx->size; ++row) {
        mtpndd_bdd_t domain = build_row_domain_bdd(ctx, row);
        bdd_and_assign(&formula, domain);
    }

    for (size_t row_a = 0; row_a < ctx->size; ++row_a) {
        for (size_t row_b = row_a + 1; row_b < ctx->size; ++row_b) {
            mtpndd_bdd_t diff = build_rows_not_equal_bdd(ctx, row_a, row_b);
            bdd_and_assign(&formula, diff);

            mtpndd_bdd_t diag = build_diagonal_pair_constraint(ctx, row_a, row_b);
            bdd_and_assign(&formula, diag);
        }
    }

    *out_formula = formula;
    return true;
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
    printf("== solving n=%zu\n", size);
    fflush(stdout);
    size_t bdd_size = (size <= 7) ? (1 << 19) : (size <= 9) ? (1 << 22) : (1 << 25);
    size_t ndd_size = (size <= 7) ? (1 << 18) : (size <= 9) ? (1 << 20) : (1 << 23);
    size_t cache_size = (size <= 7) ? (1 << 18) : (size <= 9) ? (1 << 20) : (1 << 23);

    mtpndd_pal_config_t config = {
        .n_workers = 0,
        .lace_dqsize = 1024,
        .bdd_nodetable_size = bdd_size,
        .mtpndd_nodetable_size = ndd_size,
        .op_cache_size = cache_size,
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

    printf(".. building formula\n");
    fflush(stdout);
    mtpndd_bdd_t formula_bdd = sylvan_false;
    bool formula_bdd_valid = false;
    if (!build_nqueens_bdd(&ctx, &formula_bdd)) {
        fprintf(stderr, "Failed to build formula for size %zu.\n", size);
        goto cleanup;
    }
    formula_bdd_valid = true;

    mtpndd_t *formula_node = NULL;
    if (mtbdd_to_mtpndd(formula_bdd, &formula_node) != MTPNDD_SUCCESS) {
        fprintf(stderr, "mtbdd_to_mtpndd failed for size %zu: %s\n", size,
                mtpndd_error_string(mtpndd_get_last_error().code));
        if (formula_bdd_valid) {
            sylvan_deref(formula_bdd);
            formula_bdd_valid = false;
        }
        goto cleanup;
    }
    ctx.formula = formula_node;
    printf(".. formula built\n");
    fflush(stdout);

    printf(".. running mtpndd_satcount\n");
    fflush(stdout);
    double satcount_value = mtpndd_satcount(ctx.formula);
    printf(".. satcount done\n");
    fflush(stdout);

    clock_gettime(CLOCK_MONOTONIC, &finish);
    double elapsed = timespec_to_seconds(&start, &finish);

    uint64_t solutions = (uint64_t)llround(satcount_value);
    uint64_t expected = expected_solutions(size);
    if (expected != UINT64_MAX && solutions != expected) {
        fprintf(stderr, "Unexpected solution count for size %zu: got %" PRIu64 ", expected %" PRIu64 ".\n",
                size, solutions, expected);
        goto cleanup;
    }

    size_t sylvan_nodes = sylvan_nodecount(formula_bdd);
    double satcount_verify = mtbdd_satcount(formula_bdd, total_variable_count(&ctx));
    uint64_t satcount_check = (uint64_t)llround(satcount_verify);

    if (satcount_check != solutions) {
        fprintf(stderr, "Satcount mismatch for size %zu: API %" PRIu64 ", direct %" PRIu64 ".\n",
                size, solutions, satcount_check);
        if (formula_bdd_valid) {
            sylvan_deref(formula_bdd);
            formula_bdd_valid = false;
        }
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
    if (formula_bdd_valid) {
        sylvan_deref(formula_bdd);
    }
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
        printf("== result n=%zu -> solutions=%" PRIu64 ", time=%.3f s\n",
               metrics[recorded].size,
               metrics[recorded].solutions,
               metrics[recorded].seconds);
        fflush(stdout);
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
