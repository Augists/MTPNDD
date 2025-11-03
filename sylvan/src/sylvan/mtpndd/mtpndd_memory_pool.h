// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Memory pool management for frequently allocated objects.

#ifndef MTPNDD_MEMORY_POOL_H
#define MTPNDD_MEMORY_POOL_H

#include <stddef.h>
#include "mtpndd_node.h"

struct mtpndd_nodetable_bucket_entry_s;
typedef struct mtpndd_nodetable_bucket_entry_s mtpndd_nodetable_bucket_entry_t;

mtpndd_error_t mtpndd_memory_pools_init(void);
void mtpndd_memory_pools_shutdown(void);

mtpndd_node_t *mtpndd_memory_acquire_node(void);
void mtpndd_memory_release_node(mtpndd_node_t *node);

edge_bucket_entry_t *mtpndd_memory_acquire_edge_entry(void);
void mtpndd_memory_release_edge_entry(edge_bucket_entry_t *entry);

mtpndd_edge_t *mtpndd_memory_acquire_edge_map(void);
void mtpndd_memory_release_edge_map(mtpndd_edge_t *edges);

mtpndd_nodetable_bucket_entry_t *mtpndd_memory_acquire_nodetable_entry(void);
void mtpndd_memory_release_nodetable_entry(mtpndd_nodetable_bucket_entry_t *entry);

#endif // MTPNDD_MEMORY_POOL_H
