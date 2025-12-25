// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Experiment harness for investigating performance differences vs JNDD.

#include "mtpndd.h"
#include "mtpndd_common.h"
#include "mtpndd_node.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sylvan.h"
#include "sylvan_mtbdd.h"

typedef struct {
    size_t size;
    uint32_t *field_ids;
} nqueens_ctx_t;

typedef struct {
    uint64_t row_or_ns;
    uint64_t row_or_calls;
    uint64_t row_and_ns;
    uint64_t row_and_calls;

    uint64_t imp_or_ns;
    uint64_t imp_or_calls;
    uint64_t imp_and_ns;
    uint64_t imp_and_calls;

    // Full append_implication cost (or+and+ref/deref) grouped by constraint category.
    uint64_t imp_same_row_ns;
    uint64_t imp_same_col_ns;
    uint64_t imp_diag_ns;

    uint64_t cell_combine_and_ns;
    uint64_t cell_combine_and_calls;
} build_breakdown_t;

typedef struct {
    uint64_t to_mtbdd_ns;
    uint64_t mtbdd_satcount_ns;
} sat_breakdown_t;

typedef struct {
    uint64_t min_ns;
    uint64_t p50_ns;
    uint64_t p90_ns;
    uint64_t max_ns;
    double avg_ns;
} ns_stats_t;

typedef struct {
    uint64_t init_ns;
    uint64_t declare_ns;
    uint64_t build_ns;
    uint64_t sat_ns;
    uint64_t total_ns;
    uint64_t java_like_ns; // declare + build (exclude init + sat)
    build_breakdown_t build;
    sat_breakdown_t sat;
    ns_stats_t cell_imp_build_stats;
    ns_stats_t cell_formula_and_stats;
    uint64_t nodes;
} exp_timing_t;

static void nqueens_ctx_destroy(nqueens_ctx_t *ctx);
static bool nqueens_ctx_init(nqueens_ctx_t *ctx, size_t size);
static bool build_nqueens_formula(const nqueens_ctx_t *ctx,
                                  mtpndd_t *out_formula,
                                  build_breakdown_t *breakdown,
                                  uint64_t *cell_imp_build_ns,
                                  uint64_t *cell_formula_and_ns);
static uint64_t expected_solutions(size_t size);

static inline uint64_t monotonic_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static size_t total_bdd_vars(void) {
    size_t total = 0;
    for (uint32_t i = 1; i <= g_mtpndd_config.field_count; ++i) {
        mtpndd_field_info_t *field = g_mtpndd_config.field_info[i];
        if (!field) continue;
        size_t end = (size_t)field->start_var + field->bit_width;
        if (end > total) total = end;
    }
    return total;
}

static mtpndd_t build_row_at_least_one(const nqueens_ctx_t *ctx, size_t row, build_breakdown_t *breakdown) {
    mtpndd_t accum = MTPNDD_FALSE;
    mtpndd_ref(accum);

    for (size_t col = 0; col < ctx->size; ++col) {
        uint32_t field = ctx->field_ids[row];
        mtpndd_t cell = mtpndd_get_var(field, (uint32_t)col);
        mtpndd_t old = accum;
        uint64_t start = monotonic_now_ns();
        mtpndd_t next = mtpndd_or(old, cell);
        uint64_t end = monotonic_now_ns();
        if (breakdown) {
            breakdown->row_or_ns += end - start;
            breakdown->row_or_calls += 1;
        }
        if (next == MTPNDD_INVALID) {
            mtpndd_deref(old);
            return MTPNDD_INVALID;
        }
        mtpndd_ref(next);
        mtpndd_deref(old);
        accum = next;
    }

    return accum;
}

static bool append_implication_timed(mtpndd_t *accum,
                                     mtpndd_t guard_neg,
                                     mtpndd_t neg_literal,
                                     build_breakdown_t *breakdown,
                                     uint64_t *elapsed_ns)
{
    uint64_t start_total = monotonic_now_ns();

    uint64_t start_or = monotonic_now_ns();
    mtpndd_t imp = mtpndd_or(guard_neg, neg_literal);
    uint64_t end_or = monotonic_now_ns();
    if (breakdown) {
        breakdown->imp_or_ns += end_or - start_or;
        breakdown->imp_or_calls += 1;
    }
    if (imp == MTPNDD_INVALID) {
        return false;
    }
    mtpndd_ref(imp);

    mtpndd_t old = *accum;
    uint64_t start_and = monotonic_now_ns();
    mtpndd_t next = mtpndd_and(old, imp);
    uint64_t end_and = monotonic_now_ns();
    if (breakdown) {
        breakdown->imp_and_ns += end_and - start_and;
        breakdown->imp_and_calls += 1;
    }
    mtpndd_deref(imp);
    if (next == MTPNDD_INVALID) {
        return false;
    }
    mtpndd_ref(next);
    mtpndd_deref(old);
    *accum = next;

    if (elapsed_ns) {
        uint64_t end_total = monotonic_now_ns();
        *elapsed_ns = end_total - start_total;
    }
    return true;
}

static mtpndd_t build_cell_implication(const nqueens_ctx_t *ctx,
                                       size_t row,
                                       size_t col,
                                       build_breakdown_t *breakdown)
{
    mtpndd_t accum = MTPNDD_TRUE;
    mtpndd_ref(accum);

    mtpndd_t guard_neg = mtpndd_get_not_var((uint32_t)(row + 1), (uint32_t)col);

    size_t n = ctx->size;

    for (size_t other_col = 0; other_col < n; ++other_col) {
        if (other_col == col) continue;
        uint64_t elapsed = 0;
        if (!append_implication_timed(&accum,
                                      guard_neg,
                                      mtpndd_get_not_var((uint32_t)(row + 1), (uint32_t)other_col),
                                      breakdown,
                                      &elapsed)) {
            mtpndd_deref(accum);
            return MTPNDD_INVALID;
        }
        if (breakdown) breakdown->imp_same_row_ns += elapsed;
    }

    for (size_t other_row = 0; other_row < n; ++other_row) {
        if (other_row == row) continue;
        uint64_t elapsed = 0;
        if (!append_implication_timed(&accum,
                                      guard_neg,
                                      mtpndd_get_not_var((uint32_t)(other_row + 1), (uint32_t)col),
                                      breakdown,
                                      &elapsed)) {
            mtpndd_deref(accum);
            return MTPNDD_INVALID;
        }
        if (breakdown) breakdown->imp_same_col_ns += elapsed;
    }

    for (size_t other_row = 0; other_row < n; ++other_row) {
        if (other_row == row) continue;
        long diag_up = (long)other_row - (long)row + (long)col;
        if (diag_up >= 0 && (size_t)diag_up < n) {
            uint64_t elapsed = 0;
            if (!append_implication_timed(&accum,
                                          guard_neg,
                                          mtpndd_get_not_var((uint32_t)(other_row + 1), (uint32_t)diag_up),
                                          breakdown,
                                          &elapsed)) {
                mtpndd_deref(accum);
                return MTPNDD_INVALID;
            }
            if (breakdown) breakdown->imp_diag_ns += elapsed;
        }
        long diag_down = (long)row + (long)col - (long)other_row;
        if (diag_down >= 0 && (size_t)diag_down < n) {
            uint64_t elapsed = 0;
            if (!append_implication_timed(&accum,
                                          guard_neg,
                                          mtpndd_get_not_var((uint32_t)(other_row + 1), (uint32_t)diag_down),
                                          breakdown,
                                          &elapsed)) {
                mtpndd_deref(accum);
                return MTPNDD_INVALID;
            }
            if (breakdown) breakdown->imp_diag_ns += elapsed;
        }
    }

    return accum;
}

static bool nqueens_ctx_init(nqueens_ctx_t *ctx, size_t size) {
    ctx->size = size;
    ctx->field_ids = (uint32_t *)calloc(size, sizeof(uint32_t));
    if (!ctx->field_ids) return false;
    for (size_t row = 0; row < size; ++row) {
        if (mtpndd_declare_field((uint32_t)size) != MTPNDD_SUCCESS) {
            return false;
        }
        ctx->field_ids[row] = (uint32_t)(row + 1);
    }
    return true;
}

static void nqueens_ctx_destroy(nqueens_ctx_t *ctx) {
    if (!ctx) return;
    free(ctx->field_ids);
    ctx->field_ids = NULL;
    ctx->size = 0;
}

static bool build_nqueens_formula(const nqueens_ctx_t *ctx,
                                  mtpndd_t *out_formula,
                                  build_breakdown_t *breakdown,
                                  uint64_t *cell_imp_build_ns,
                                  uint64_t *cell_formula_and_ns)
{
    size_t n = ctx->size;
    bool ok = false;
    mtpndd_t formula = MTPNDD_TRUE;
    mtpndd_ref(formula);

    for (size_t row = 0; row < n; ++row) {
        mtpndd_t at_least = build_row_at_least_one(ctx, row, breakdown);
        if (at_least == MTPNDD_INVALID) goto cleanup;
        mtpndd_t old = formula;
        uint64_t start = monotonic_now_ns();
        mtpndd_t next = mtpndd_and(old, at_least);
        uint64_t end = monotonic_now_ns();
        if (breakdown) {
            breakdown->row_and_ns += end - start;
            breakdown->row_and_calls += 1;
        }
        mtpndd_deref(at_least);
        if (next == MTPNDD_INVALID) {
            mtpndd_deref(old);
            formula = MTPNDD_INVALID;
            goto cleanup;
        }
        mtpndd_ref(next);
        mtpndd_deref(old);
        formula = next;
    }

    for (size_t row = 0; row < n; ++row) {
        for (size_t col = 0; col < n; ++col) {
            size_t cell_index = row * n + col;

            uint64_t start_imp = monotonic_now_ns();
            mtpndd_t imp = build_cell_implication(ctx, row, col, breakdown);
            uint64_t end_imp = monotonic_now_ns();
            if (cell_imp_build_ns) {
                cell_imp_build_ns[cell_index] = end_imp - start_imp;
            }
            if (imp == MTPNDD_INVALID) goto cleanup;
            mtpndd_t old = formula;
            uint64_t start = monotonic_now_ns();
            mtpndd_t next = mtpndd_and(old, imp);
            uint64_t end = monotonic_now_ns();
            if (breakdown) {
                breakdown->cell_combine_and_ns += end - start;
                breakdown->cell_combine_and_calls += 1;
            }
            if (cell_formula_and_ns) {
                cell_formula_and_ns[cell_index] = end - start;
            }
            mtpndd_deref(imp);
            if (next == MTPNDD_INVALID) {
                mtpndd_deref(old);
                formula = MTPNDD_INVALID;
                goto cleanup;
            }
            mtpndd_ref(next);
            mtpndd_deref(old);
            formula = next;
        }
    }

    *out_formula = formula;
    formula = MTPNDD_INVALID;
    ok = true;

cleanup:
    if (formula != MTPNDD_INVALID) mtpndd_deref(formula);
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

static int compare_u64(const void *lhs, const void *rhs) {
    uint64_t a = *(const uint64_t *)lhs;
    uint64_t b = *(const uint64_t *)rhs;
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

static void compute_ns_stats(uint64_t *samples, size_t count, ns_stats_t *out) {
    if (!out) return;
    *out = (ns_stats_t){0};
    if (!samples || count == 0) return;

    unsigned __int128 sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += samples[i];
    }

    qsort(samples, count, sizeof(uint64_t), compare_u64);

    out->min_ns = samples[0];
    out->max_ns = samples[count - 1];
    out->p50_ns = samples[count / 2];

    size_t p90_index = (size_t)ceil(0.9 * (double)count) - 1;
    if (p90_index >= count) p90_index = count - 1;
    out->p90_ns = samples[p90_index];

    out->avg_ns = (double)sum / (double)count;
}

static void fill_baseline_config(size_t n, mtpndd_pal_config_t *config) {
    size_t bdd_size = 1 << 19;
    size_t ndd_size = 1 << 21;
    size_t cache_size = 1 << 18;
    size_t gc_bucket_count = 256;

    if (n > 6 && n <= 8) {
        bdd_size = 1 << 20;
        ndd_size = 1 << 22;
        cache_size = 1 << 19;
        gc_bucket_count = 512;
    } else if (n > 8 && n <= 10) {
        bdd_size = 1 << 21;
        ndd_size = 1 << 23;
        cache_size = 1 << 20;
        gc_bucket_count = 1024;
    } else if (n > 10) {
        bdd_size = 1 << 22;
        ndd_size = 1 << 24;
        cache_size = 1 << 21;
        gc_bucket_count = 2048;
    }

    size_t nodetable_bucket_count = ndd_size;

    *config = (mtpndd_pal_config_t){
            .n_workers = 0,
            .lace_dqsize = 1024,
            .bdd_nodetable_size = bdd_size,
            .mtpndd_nodetable_size = ndd_size,
            .op_cache_size = cache_size,
            .quick_growth_threshold = 0.1,
            .nodetable_bucket_count = nodetable_bucket_count,
            .gc_bucket_count = gc_bucket_count,
    };
}

static void apply_sylvan_limits(mtpndd_pal_config_t *config, size_t memory_cap_bytes, int ratio, int initial, int granularity) {
    config->sylvan_memory_cap = memory_cap_bytes;
    config->sylvan_table_ratio = ratio;
    config->sylvan_initial_ratio = initial;
    config->sylvan_granularity = granularity;
}

static void apply_small_growth_config(mtpndd_pal_config_t *config,
                                      size_t node_min,
                                      size_t hash_min,
                                      size_t node_max,
                                      size_t hash_max,
                                      size_t op_cache_size)
{
    config->mtpndd_nodetable_size = node_min;
    config->mtpndd_nodetable_max_size = node_max;
    config->nodetable_bucket_count = hash_min;
    config->nodetable_bucket_max_count = hash_max;
    config->mtpndd_op_cache_size = op_cache_size;
}

static bool run_case(size_t n, const mtpndd_pal_config_t *config, exp_timing_t *timing_out, uint64_t *solutions_out) {
    bool ok = false;
    nqueens_ctx_t ctx = {0};
    mtpndd_t formula = MTPNDD_INVALID;
    uint64_t *cell_imp_build_ns = NULL;
    uint64_t *cell_formula_and_ns = NULL;

    uint64_t t0 = monotonic_now_ns();

    mtpndd_pal_config_t local_cfg = *config;
    if (mtpndd_init(&local_cfg) != MTPNDD_SUCCESS) {
        fprintf(stderr, "mtpndd_init failed: %s\n", mtpndd_error_string(mtpndd_get_last_error().code));
        return false;
    }
    uint64_t t1 = monotonic_now_ns();

    if (!nqueens_ctx_init(&ctx, n)) {
        fprintf(stderr, "Failed to initialise context.\n");
        goto cleanup;
    }
    uint64_t t2 = monotonic_now_ns();

    build_breakdown_t build = {0};

    size_t cell_count = n * n;
    cell_imp_build_ns = (uint64_t *)calloc(cell_count, sizeof(uint64_t));
    cell_formula_and_ns = (uint64_t *)calloc(cell_count, sizeof(uint64_t));
    if (!cell_imp_build_ns || !cell_formula_and_ns) {
        fprintf(stderr, "Out of memory allocating per-cell timing arrays.\n");
        goto cleanup;
    }

    if (!build_nqueens_formula(&ctx, &formula, &build, cell_imp_build_ns, cell_formula_and_ns)) {
        fprintf(stderr, "Failed to build formula.\n");
        goto cleanup;
    }
    uint64_t t3 = monotonic_now_ns();

    sat_breakdown_t sat = {0};
    mtpndd_bdd_t bdd = sylvan_false;
    uint64_t start_conv = monotonic_now_ns();
    mtpndd_error_t status = mtpndd_to_mtbdd(formula, &bdd);
    uint64_t end_conv = monotonic_now_ns();
    sat.to_mtbdd_ns = end_conv - start_conv;
    if (status != MTPNDD_SUCCESS) {
        fprintf(stderr, "mtpndd_to_mtbdd failed: %s\n", mtpndd_error_string(mtpndd_get_last_error().code));
        goto cleanup;
    }
    size_t nvars = total_bdd_vars();
    uint64_t start_sat = monotonic_now_ns();
    double satcount_value = mtbdd_satcount(bdd, nvars);
    uint64_t end_sat = monotonic_now_ns();
    sat.mtbdd_satcount_ns = end_sat - start_sat;
    sylvan_deref(bdd);

    uint64_t t4 = monotonic_now_ns();

    uint64_t solutions = (uint64_t)llround(satcount_value);
    uint64_t expected = expected_solutions(n);
    if (expected != UINT64_MAX && solutions != expected) {
        fprintf(stderr, "Unexpected solution count for size %zu: got %" PRIu64 ", expected %" PRIu64 ".\n",
                n, solutions, expected);
        goto cleanup;
    }

    if (solutions_out) {
        *solutions_out = solutions;
    }

    if (timing_out) {
        timing_out->init_ns = t1 - t0;
        timing_out->declare_ns = t2 - t1;
        timing_out->build_ns = t3 - t2;
        timing_out->sat_ns = t4 - t3;
        timing_out->total_ns = t4 - t0;
        timing_out->java_like_ns = t3 - t1;
        timing_out->build = build;
        timing_out->sat = sat;
        compute_ns_stats(cell_imp_build_ns, cell_count, &timing_out->cell_imp_build_stats);
        compute_ns_stats(cell_formula_and_ns, cell_count, &timing_out->cell_formula_and_stats);
        const mtpndd_stats_t *stats = mtpndd_get_stats();
        timing_out->nodes = stats ? stats->node_count : 0;
    }

    free(cell_imp_build_ns);
    cell_imp_build_ns = NULL;
    free(cell_formula_and_ns);
    cell_formula_and_ns = NULL;
    ok = true;

cleanup:
    free(cell_imp_build_ns);
    free(cell_formula_and_ns);
    if (formula != MTPNDD_INVALID) {
        mtpndd_deref(formula);
        formula = MTPNDD_INVALID;
    }
    nqueens_ctx_destroy(&ctx);
    if (mtpndd_quit() != MTPNDD_SUCCESS) {
        fprintf(stderr, "mtpndd_quit reported an error.\n");
        ok = false;
    }
    return ok;
}

static double ns_to_s(uint64_t ns) {
    return (double)ns / 1e9;
}

static double ns_to_ms(double ns) {
    return ns / 1e6;
}

static void print_header(void) {
    printf("experiment\tinit\tdeclare\tbuild\tsat(to_mtbdd+satcount)\ttotal\tjava_like(declare+build)\tnodes\n");
}

static void print_row(const char *name, const exp_timing_t *t) {
    printf("%s\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%" PRIu64 "\n",
           name,
           ns_to_s(t->init_ns),
           ns_to_s(t->declare_ns),
           ns_to_s(t->build_ns),
           ns_to_s(t->sat_ns),
           ns_to_s(t->total_ns),
           ns_to_s(t->java_like_ns),
           t->nodes);

    printf("  build_breakdown(s): row_or=%.3f(%" PRIu64 ") row_and=%.3f(%" PRIu64 ") "
           "imp_or=%.3f(%" PRIu64 ") imp_and=%.3f(%" PRIu64 ") cell_and=%.3f(%" PRIu64 ")\n",
           ns_to_s(t->build.row_or_ns), t->build.row_or_calls,
           ns_to_s(t->build.row_and_ns), t->build.row_and_calls,
           ns_to_s(t->build.imp_or_ns), t->build.imp_or_calls,
           ns_to_s(t->build.imp_and_ns), t->build.imp_and_calls,
           ns_to_s(t->build.cell_combine_and_ns), t->build.cell_combine_and_calls);
    printf("  implication_groups(s): same_row=%.3f same_col=%.3f diag=%.3f\n",
           ns_to_s(t->build.imp_same_row_ns),
           ns_to_s(t->build.imp_same_col_ns),
           ns_to_s(t->build.imp_diag_ns));
    printf("  sat_breakdown(s): to_mtbdd=%.3f mtbdd_satcount=%.3f\n",
           ns_to_s(t->sat.to_mtbdd_ns),
           ns_to_s(t->sat.mtbdd_satcount_ns));

    printf("  per_cell(ms): imp_build avg=%.3f p50=%.3f p90=%.3f max=%.3f\n",
           ns_to_ms(t->cell_imp_build_stats.avg_ns),
           ns_to_ms((double)t->cell_imp_build_stats.p50_ns),
           ns_to_ms((double)t->cell_imp_build_stats.p90_ns),
           ns_to_ms((double)t->cell_imp_build_stats.max_ns));
    printf("  per_cell(ms): formula_and avg=%.3f p50=%.3f p90=%.3f max=%.3f\n",
           ns_to_ms(t->cell_formula_and_stats.avg_ns),
           ns_to_ms((double)t->cell_formula_and_stats.p50_ns),
           ns_to_ms((double)t->cell_formula_and_stats.p90_ns),
           ns_to_ms((double)t->cell_formula_and_stats.max_ns));
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <n>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    long parsed = strtol(argv[1], &endptr, 10);
    if (*argv[1] == '\0' || (endptr && *endptr != '\0') || parsed <= 0) {
        fprintf(stderr, "Invalid n value: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    size_t n = (size_t)parsed;

    // Baseline configuration (mirrors mtpndd_nqueens_test).
    mtpndd_pal_config_t baseline = {0};
    fill_baseline_config(n, &baseline);

    // Derive "baseline max" from baseline itself (fixed-size mode uses min==max).
    size_t baseline_node_max = baseline.mtpndd_nodetable_size;
    size_t baseline_hash_max = baseline.nodetable_bucket_count;

    // Small-growth configuration tuned for small n: start closer to expected working set, grow up to baseline.
    mtpndd_pal_config_t small = baseline;
    size_t node_min = (n <= 8) ? (1u << 14) : (n <= 10) ? (1u << 15) : (1u << 16);
    size_t hash_min = node_min * 2;
    apply_small_growth_config(&small,
                              node_min,
                              hash_min,
                              baseline_node_max,
                              baseline_hash_max,
                              10000);

    // Sylvan limits config (mimic JSylvan.init(..., maxMemory, tableRatio=1, initialRatio=4, granularity=1)).
    const size_t eight_gb = 8ULL * 1024 * 1024 * 1024;
    mtpndd_pal_config_t baseline_limits = baseline;
    apply_sylvan_limits(&baseline_limits, eight_gb, 1, 4, 1);

    mtpndd_pal_config_t small_limits = small;
    apply_sylvan_limits(&small_limits, eight_gb, 1, 4, 1);

    print_header();

    uint64_t solutions = 0;
    exp_timing_t t = {0};

    if (!run_case(n, &baseline, &t, &solutions)) return EXIT_FAILURE;
    print_row("baseline_set_sizes", &t);

    if (!run_case(n, &baseline_limits, &t, &solutions)) return EXIT_FAILURE;
    print_row("baseline_sylvan_limits", &t);

    if (!run_case(n, &small, &t, &solutions)) return EXIT_FAILURE;
    print_row("small_growth_set_sizes", &t);

    if (!run_case(n, &small_limits, &t, &solutions)) return EXIT_FAILURE;
    print_row("small_growth_sylvan_limits", &t);

    printf("solutions=%" PRIu64 "\n", solutions);
    return EXIT_SUCCESS;
}
