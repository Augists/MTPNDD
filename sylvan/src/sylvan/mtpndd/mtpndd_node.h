// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_NODE_H
#define MTPNDD_NODE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "mtpndd_common.h"

static inline bool mtpndd_is_true(mtpndd_t ndd) {
    return ndd == MTPNDD_TRUE;
}

static inline bool mtpndd_is_false(mtpndd_t ndd) {
    return ndd == MTPNDD_FALSE;
}

static inline bool mtpndd_is_terminal(mtpndd_t ndd) {
    // TODO: support multi-terminal
    return ndd < 2;
}

/********************************
 * MTPNDD operations
 ********************************/
mtpndd_t mtpndd_and(mtpndd_t a, mtpndd_t b);
mtpndd_t mtpndd_or(mtpndd_t a, mtpndd_t b);
mtpndd_t mtpndd_not(mtpndd_t a);
mtpndd_t mtpndd_diff(mtpndd_t a, mtpndd_t b);
mtpndd_t mtpndd_exist(mtpndd_t a, uint32_t field);
double mtpndd_satcount(mtpndd_t node);

/********************************
 * MTPNDD <-> MTBDD conversion
 ********************************/
mtpndd_error_t mtpndd_to_mtbdd(mtpndd_t node, mtpndd_bdd_t *result);
mtpndd_error_t mtbdd_to_mtpndd(mtpndd_bdd_t bdd, mtpndd_t *result);

void mtpndd_fprint_dot(FILE *out, mtpndd_t root);
void mtpndd_print_dot(mtpndd_t root);

#endif // MTPNDD_NODE_H
