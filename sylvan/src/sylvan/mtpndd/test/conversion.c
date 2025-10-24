#include "mtpndd.h"
#include "sylvan.h"

#include <assert.h>
#include <stdio.h>

static void assert_success(mtpndd_error_t err)
{
    if (err != MTPNDD_SUCCESS) {
        mtpndd_error_info_t info = mtpndd_get_last_error();
        fprintf(stderr, "MTPNDD error %d (%s) at %s:%d\n",
                err, mtpndd_error_string(err), info.function ? info.function : "?", info.line);
        assert(false);
    }
}

int main(void)
{
    mtpndd_pal_config_t config = {
        .n_workers = 1,
        .lace_dqsize = 1 << 18,
        .bdd_nodetable_size = 1 << 16,
        .mtpndd_nodetable_size = 1 << 14,
        .op_cache_size = 1 << 12,
    };
    assert_success(mtpndd_init(&config));

    assert_success(mtpndd_declare_field(2));
    assert_success(mtpndd_declare_field(1));

    // Terminal conversions
    mtpndd_bdd_t terminal_bdd = sylvan_false;
    mtpndd_t *terminal_node = NULL;

    assert_success(mtpndd_to_mtbdd(&MTPNDD_FALSE, &terminal_bdd));
    assert(terminal_bdd == sylvan_false);
    assert_success(mtbdd_to_mtpndd(sylvan_false, &terminal_node));
    assert(terminal_node == &MTPNDD_FALSE);

    assert_success(mtpndd_to_mtbdd(&MTPNDD_TRUE, &terminal_bdd));
    assert(terminal_bdd == sylvan_true);
    assert_success(mtbdd_to_mtpndd(sylvan_true, &terminal_node));
    assert(terminal_node == &MTPNDD_TRUE);

    mtpndd_t *var_f1_bit0 = mtpndd_get_var(1, 0);
    mtpndd_t *not_f1_bit1 = mtpndd_get_not_var(1, 1);
    mtpndd_t *var_f2_bit0 = mtpndd_get_var(2, 0);
    assert(var_f1_bit0 && not_f1_bit1 && var_f2_bit0);

    // Direct node conversions
    mtpndd_bdd_t node_bdd = sylvan_false;
    assert_success(mtpndd_to_mtbdd(var_f1_bit0, &node_bdd));
    assert(node_bdd == mtpndd_get_bdd_var(1, 0));
    sylvan_deref(node_bdd);

    assert_success(mtpndd_to_mtbdd(not_f1_bit1, &node_bdd));
    assert(node_bdd == mtpndd_get_bdd_not_var(1, 1));
    sylvan_deref(node_bdd);

    // Build an MTBDD formula, convert to MTPNDD, and back.
    mtpndd_bdd_t formula_bdd = sylvan_ref(sylvan_or(
            mtpndd_get_bdd_var(1, 0),
            mtpndd_get_bdd_not_var(1, 1)));
    mtpndd_t *formula_node = NULL;
    assert_success(mtbdd_to_mtpndd(formula_bdd, &formula_node));
    assert(formula_node != NULL);

    node_bdd = sylvan_false;
    assert_success(mtpndd_to_mtbdd(formula_node, &node_bdd));
    assert(node_bdd == formula_bdd);
    sylvan_deref(node_bdd);
    sylvan_deref(formula_bdd);

    // Build a complex MTBDD involving multiple fields.
    mtpndd_bdd_t bdd_a = sylvan_ref(mtpndd_get_bdd_var(1, 0));
    mtpndd_bdd_t bdd_b = sylvan_ref(mtpndd_get_bdd_var(1, 1));
    mtpndd_bdd_t bdd_c = sylvan_ref(mtpndd_get_bdd_var(2, 0));
    mtpndd_bdd_t bdd_b_and_c = sylvan_ref(sylvan_and(bdd_b, bdd_c));
    mtpndd_bdd_t complex_bdd = sylvan_ref(sylvan_xor(bdd_a, bdd_b_and_c));

    sylvan_deref(bdd_b_and_c);
    sylvan_deref(bdd_a);
    sylvan_deref(bdd_b);
    sylvan_deref(bdd_c);

    mtpndd_t *complex_node = NULL;
    assert_success(mtbdd_to_mtpndd(complex_bdd, &complex_node));
    assert(complex_node != NULL);

    node_bdd = sylvan_false;
    assert_success(mtpndd_to_mtbdd(complex_node, &node_bdd));
    assert(node_bdd == complex_bdd);
    sylvan_deref(node_bdd);
    sylvan_deref(complex_bdd);
    assert_success(mtpndd_quit());
    return 0;
}
