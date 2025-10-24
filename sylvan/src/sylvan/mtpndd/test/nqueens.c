#include "mtpndd.h"
#include "sylvan.h"
#include "sylvan_table.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

static inline size_t
cell_index(size_t row, size_t col, size_t size)
{
    return row * size + col;
}

typedef struct {
    size_t size;
    const mtpndd_field_info_t **fields;
    mtpndd_t **positive;
    mtpndd_t **negative;
    mtpndd_t *formula;
} nqueens_ctx_t;

static inline mtpndd_t *
true_node(void)
{
    return &MTPNDD_TRUE;
}

static inline mtpndd_t *
false_node(void)
{
    return &MTPNDD_FALSE;
}

static bool
nqueens_ctx_init(nqueens_ctx_t *ctx, size_t size)
{
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

static void
nqueens_ctx_destroy(nqueens_ctx_t *ctx)
{
    free(ctx->fields);
    free(ctx->positive);
    free(ctx->negative);
    ctx->fields = NULL;
    ctx->positive = NULL;
    ctx->negative = NULL;
    ctx->formula = NULL;
    ctx->size = 0;
}

static mtpndd_t *
build_row_constraint(const nqueens_ctx_t *ctx, size_t row)
{
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

static mtpndd_t *
build_nqueens_formula(nqueens_ctx_t *ctx)
{
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

static bool
bdd_eval(BDD bdd, const bool *assignment)
{
    bool invert = false;
    while (true) {
        if (bdd == sylvan_true) {
            return !invert;
        }
        if (bdd == sylvan_false) {
            return invert;
        }
        if (bdd & sylvan_complement) {
            invert = !invert;
            bdd ^= sylvan_complement;
            continue;
        }
        uint32_t var = sylvan_var(bdd);
        bdd = assignment[var] ? sylvan_high(bdd) : sylvan_low(bdd);
    }
}

static bool
mtpndd_assignment_satisfies(const mtpndd_t *node, const bool *assignment)
{
    if (mtpndd_is_true((mtpndd_t *)node)) {
        return true;
    }
    if (mtpndd_is_false((mtpndd_t *)node)) {
        return false;
    }
    edge_bucket_entry_t *entry;
    FOR_EACH_ENTRY_IN_ALL_BUCKETS(node->edges, entry) {
        mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_acquire);
        if (label == sylvan_false) {
            continue;
        }
        if (bdd_eval(label, assignment) && mtpndd_assignment_satisfies(entry->child, assignment)) {
            return true;
        }
    }
    return false;
}

static void
fill_assignment_bits(const nqueens_ctx_t *ctx, uint64_t mask, bool *assignment, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        assignment[i] = false;
    }
    for (size_t row = 0; row < ctx->size; ++row) {
        const mtpndd_field_info_t *field = ctx->fields[row];
        uint32_t start = field->start_var;
        for (size_t col = 0; col < ctx->size; ++col) {
            size_t bit = cell_index(row, col, ctx->size);
            assignment[start + col] = ((mask >> bit) & 1u) != 0;
        }
    }
}

static size_t
total_variable_count(const nqueens_ctx_t *ctx)
{
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

static size_t
brute_force_solution_count(const nqueens_ctx_t *ctx)
{
    size_t total_cells = ctx->size * ctx->size;
    if (total_cells >= 63) {
        return SIZE_MAX;
    }
    uint64_t limit = 1ULL << total_cells;
    size_t var_count = total_variable_count(ctx);
    bool *assignment = (bool *)malloc(var_count * sizeof(bool));
    if (!assignment) {
        return SIZE_MAX;
    }

    size_t solutions = 0;
    for (uint64_t mask = 0; mask < limit; ++mask) {
        fill_assignment_bits(ctx, mask, assignment, var_count);
        if (mtpndd_assignment_satisfies(ctx->formula, assignment)) {
            ++solutions;
        }
    }

    free(assignment);
    return solutions;
}

static size_t
nqueens_satcount_stub(const nqueens_ctx_t *ctx)
{
    (void)ctx;
    return SIZE_MAX;
}

static size_t
expected_solutions(size_t size)
{
    switch (size) {
    case 1:
        return 1;
    case 2:
    case 3:
        return 0;
    case 4:
        return 2;
    default:
        return SIZE_MAX;
    }
}

static bool
run_case(size_t size)
{
    mtpndd_pal_config_t config = {
        .n_workers = 1,
        .lace_dqsize = 1024,
        .bdd_nodetable_size = 1 << 15,
        .mtpndd_nodetable_size = 1 << 13,
        .op_cache_size = 1 << 15,
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

    if (!build_nqueens_formula(&ctx)) {
        fprintf(stderr, "Failed to build formula for size %zu.\n", size);
        goto cleanup;
    }

    size_t enumerated = brute_force_solution_count(&ctx);
    size_t expected = expected_solutions(size);
    if (expected == SIZE_MAX || enumerated != expected) {
        fprintf(stderr, "Unexpected solution count for size %zu: got %zu, expected %zu.\n",
                size, enumerated, expected);
        goto cleanup;
    }

    if (nqueens_satcount_stub(&ctx) != SIZE_MAX) {
        fprintf(stderr, "Satcount stub returned unexpected value for size %zu.\n", size);
        goto cleanup;
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

int
main(void)
{
    const size_t tests[] = {1, 2, 3, 4};
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!run_case(tests[i])) {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
