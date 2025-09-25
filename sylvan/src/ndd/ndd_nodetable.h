// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef NDD_NODETABLE_H
#define NDD_NODETABLE_H

#include <stdint.h>
#include "common.h"
#include "ndd.h"

// Node table lifecycle
int ndd_nodetable_init(uint32_t initial_fields_capacity);
void ndd_nodetable_shutdown();

// Ensure table for specified field is created
int ndd_nodetable_ensure_field(uint32_t field_id);

// Node interning (deduplication), equivalent to Java mk's duplicate checking and reuse
ndd_t ndd_intern_node(ndd_t node);

#endif // NDD_NODETABLE_H


