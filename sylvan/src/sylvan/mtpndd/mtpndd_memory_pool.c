// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Memory pool implementation for frequently created structures.

#include "mtpndd_memory_pool.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "mtpndd_common.h"
#include "mtpndd_nodetable.h"

typedef struct mtpndd_slab_block_s {
    struct mtpndd_slab_block_s *next;
    /* Flexible array member containing objects follows */
    unsigned char data[];
} mtpndd_slab_block_t;

typedef struct mtpndd_slab_pool_s {
    size_t object_size;
    size_t objects_per_slab;
    void *free_list;
    mtpndd_slab_block_t *blocks;
    pthread_mutex_t lock;
    size_t slab_count;
    size_t in_use;
} mtpndd_slab_pool_t;

static mtpndd_slab_pool_t g_node_pool = {0};
static mtpndd_slab_pool_t g_edge_entry_pool = {0};
static mtpndd_slab_pool_t g_nodetable_entry_pool = {0};
static mtpndd_slab_pool_t g_edge_map_pool = {0};

static size_t mtpndd_align_size(size_t size) {
    size_t alignment = sizeof(void *);
    return (size + alignment - 1) & ~(alignment - 1);
}

static void mtpndd_slab_pool_reset(mtpndd_slab_pool_t *pool) {
    pool->object_size = 0;
    pool->objects_per_slab = 0;
    pool->free_list = NULL;
    pool->blocks = NULL;
    pool->slab_count = 0;
    pool->in_use = 0;
    pthread_mutex_init(&pool->lock, NULL);
}

static void mtpndd_slab_pool_destroy(mtpndd_slab_pool_t *pool) {
    pthread_mutex_lock(&pool->lock);
    mtpndd_slab_block_t *block = pool->blocks;
    while (block) {
        mtpndd_slab_block_t *next = block->next;
        free(block);
        block = next;
    }
    pool->blocks = NULL;
    pool->free_list = NULL;
    pthread_mutex_unlock(&pool->lock);
    pthread_mutex_destroy(&pool->lock);
}

static mtpndd_error_t mtpndd_slab_pool_grow(mtpndd_slab_pool_t *pool) {
    size_t object_size = pool->object_size;
    size_t capacity = pool->objects_per_slab;
    if (capacity == 0) {
        capacity = 1;
    }
    size_t block_size = sizeof(mtpndd_slab_block_t) + object_size * capacity;
    mtpndd_slab_block_t *block = (mtpndd_slab_block_t *)malloc(block_size);
    if (!block) {
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    block->next = pool->blocks;
    pool->blocks = block;
    pool->slab_count++;

    unsigned char *cursor = block->data;
    for (size_t i = 0; i < capacity; ++i) {
        void *object = cursor + (i * object_size);
        *((void **)object) = pool->free_list;
        pool->free_list = object;
    }
    return MTPNDD_SUCCESS;
}

static void *mtpndd_slab_pool_acquire(mtpndd_slab_pool_t *pool, bool *new_slab) {
    void *result = NULL;
    pthread_mutex_lock(&pool->lock);
    if (!pool->free_list) {
        if (mtpndd_slab_pool_grow(pool) != MTPNDD_SUCCESS) {
            pthread_mutex_unlock(&pool->lock);
            return NULL;
        }
        if (new_slab) {
            *new_slab = true;
        }
    }
    result = pool->free_list;
    pool->free_list = *((void **)result);
    pool->in_use++;
    pthread_mutex_unlock(&pool->lock);
    return result;
}

static void mtpndd_slab_pool_release(mtpndd_slab_pool_t *pool, void *object) {
    if (!object) {
        return;
    }
    pthread_mutex_lock(&pool->lock);
    *((void **)object) = pool->free_list;
    pool->free_list = object;
    if (pool->in_use > 0) {
        pool->in_use--;
    }
    pthread_mutex_unlock(&pool->lock);
}

static mtpndd_error_t mtpndd_slab_pool_setup(mtpndd_slab_pool_t *pool, size_t object_size, size_t objects_per_slab) {
    mtpndd_slab_pool_reset(pool);
    size_t aligned_size = mtpndd_align_size(object_size);
    if (aligned_size < sizeof(void *)) {
        aligned_size = sizeof(void *);
    }
    pool->object_size = aligned_size;
    pool->objects_per_slab = objects_per_slab ? objects_per_slab : 1;
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_memory_pools_init(void) {
    size_t node_capacity = mtpndd_config_node_slab_capacity();
    size_t edge_capacity = mtpndd_config_edge_entry_slab_capacity();
    size_t nodetable_capacity = mtpndd_config_nodetable_entry_slab_capacity();
    size_t edge_map_capacity = mtpndd_config_edge_map_slab_capacity();

    mtpndd_error_t status = mtpndd_slab_pool_setup(&g_node_pool, sizeof(mtpndd_node_t), node_capacity);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    status = mtpndd_slab_pool_setup(&g_edge_map_pool, sizeof(mtpndd_edge_t), edge_map_capacity);
    if (status != MTPNDD_SUCCESS) {
        mtpndd_slab_pool_destroy(&g_node_pool);
        return status;
    }

    status = mtpndd_slab_pool_setup(&g_edge_entry_pool, sizeof(edge_bucket_entry_t), edge_capacity);
    if (status != MTPNDD_SUCCESS) {
        mtpndd_slab_pool_destroy(&g_edge_map_pool);
        mtpndd_slab_pool_destroy(&g_node_pool);
        return status;
    }

    status = mtpndd_slab_pool_setup(&g_nodetable_entry_pool, sizeof(mtpndd_nodetable_bucket_entry_t), nodetable_capacity);
    if (status != MTPNDD_SUCCESS) {
        mtpndd_slab_pool_destroy(&g_edge_entry_pool);
        mtpndd_slab_pool_destroy(&g_edge_map_pool);
        mtpndd_slab_pool_destroy(&g_node_pool);
        return status;
    }

    return MTPNDD_SUCCESS;
}

void mtpndd_memory_pools_shutdown(void) {
    mtpndd_slab_pool_destroy(&g_edge_map_pool);
    mtpndd_slab_pool_destroy(&g_nodetable_entry_pool);
    mtpndd_slab_pool_destroy(&g_edge_entry_pool);
    mtpndd_slab_pool_destroy(&g_node_pool);
}

void mtpndd_memory_pools_snapshot(mtpndd_memory_pool_stats_t *stats) {
    if (!stats) return;
    pthread_mutex_lock(&g_node_pool.lock);
    stats->node_slabs = g_node_pool.slab_count;
    stats->node_in_use = g_node_pool.in_use;
    stats->node_capacity_per_slab = g_node_pool.objects_per_slab;
    pthread_mutex_unlock(&g_node_pool.lock);

    pthread_mutex_lock(&g_edge_entry_pool.lock);
    stats->edge_entry_slabs = g_edge_entry_pool.slab_count;
    stats->edge_entry_in_use = g_edge_entry_pool.in_use;
    stats->edge_entry_capacity_per_slab = g_edge_entry_pool.objects_per_slab;
    pthread_mutex_unlock(&g_edge_entry_pool.lock);

    pthread_mutex_lock(&g_nodetable_entry_pool.lock);
    stats->nodetable_entry_slabs = g_nodetable_entry_pool.slab_count;
    stats->nodetable_entry_in_use = g_nodetable_entry_pool.in_use;
    stats->nodetable_entry_capacity_per_slab = g_nodetable_entry_pool.objects_per_slab;
    pthread_mutex_unlock(&g_nodetable_entry_pool.lock);

    pthread_mutex_lock(&g_edge_map_pool.lock);
    stats->edge_map_slabs = g_edge_map_pool.slab_count;
    stats->edge_map_in_use = g_edge_map_pool.in_use;
    stats->edge_map_capacity_per_slab = g_edge_map_pool.objects_per_slab;
    pthread_mutex_unlock(&g_edge_map_pool.lock);
}

mtpndd_node_t *mtpndd_memory_acquire_node(void) {
    bool grew = false;
    mtpndd_node_t *node = (mtpndd_node_t *)mtpndd_slab_pool_acquire(&g_node_pool, &grew);
    if (!node) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(node_pool_acquire_total, 1);
    if (grew) {
        MTPNDD_STAT_ADD(node_pool_slab_total, 1);
    }
#endif
    return node;
}

void mtpndd_memory_release_node(mtpndd_node_t *node) {
    if (!node) {
        return;
    }
    mtpndd_slab_pool_release(&g_node_pool, node);
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(node_pool_release_total, 1);
#endif
}

edge_bucket_entry_t *mtpndd_memory_acquire_edge_entry(void) {
    bool grew = false;
    edge_bucket_entry_t *entry = (edge_bucket_entry_t *)mtpndd_slab_pool_acquire(&g_edge_entry_pool, &grew);
    if (!entry) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(edge_entry_pool_acquire_total, 1);
    if (grew) {
        MTPNDD_STAT_ADD(edge_entry_pool_slab_total, 1);
    }
#endif
    return entry;
}

void mtpndd_memory_release_edge_entry(edge_bucket_entry_t *entry) {
    if (!entry) {
        return;
    }
    mtpndd_slab_pool_release(&g_edge_entry_pool, entry);
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(edge_entry_pool_release_total, 1);
#endif
}

mtpndd_nodetable_bucket_entry_t *mtpndd_memory_acquire_nodetable_entry(void) {
    bool grew = false;
    mtpndd_nodetable_bucket_entry_t *entry = (mtpndd_nodetable_bucket_entry_t *)mtpndd_slab_pool_acquire(&g_nodetable_entry_pool, &grew);
    if (!entry) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(nodetable_entry_pool_acquire_total, 1);
    if (grew) {
        MTPNDD_STAT_ADD(nodetable_entry_pool_slab_total, 1);
    }
#endif
    return entry;
}

void mtpndd_memory_release_nodetable_entry(mtpndd_nodetable_bucket_entry_t *entry) {
    if (!entry) {
        return;
    }
    mtpndd_slab_pool_release(&g_nodetable_entry_pool, entry);
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(nodetable_entry_pool_release_total, 1);
#endif
}

mtpndd_edge_t *mtpndd_memory_acquire_edge_map(void) {
    bool grew = false;
    mtpndd_edge_t *edges = (mtpndd_edge_t *)mtpndd_slab_pool_acquire(&g_edge_map_pool, &grew);
    if (!edges) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(edge_map_pool_acquire_total, 1);
    if (grew) {
        MTPNDD_STAT_ADD(edge_map_pool_slab_total, 1);
    }
#endif
    return edges;
}

void mtpndd_memory_release_edge_map(mtpndd_edge_t *edges) {
    if (!edges) {
        return;
    }
    mtpndd_slab_pool_release(&g_edge_map_pool, edges);
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(edge_map_pool_release_total, 1);
#endif
}
