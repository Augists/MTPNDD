// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#ifdef HAVE_LACE_SYLVAN
// Real Lace + Sylvan integration
#include <lace.h>
#include <sylvan.h>
#include <sylvan_bdd.h>

// Global Lace + Sylvan state
static struct {
    bool initialized;
    uint32_t n_workers;
    size_t dqsize;
    size_t table_size;
    size_t cache_size;
} g_parallel_state = {0};

// Real Lace + Sylvan initialization
int ndd_lace_init(uint32_t n_workers, size_t dqsize) {
    if (g_parallel_state.initialized) {
        return 0;
    }
    
    printf("🚀 Initializing Lace + Sylvan parallel framework...\n");
    
    // Initialize Lace work-stealing framework
    lace_start(n_workers, dqsize);
    
    // Initialize Sylvan BDD library
    sylvan_set_sizes(1LL<<24, 1LL<<20, 1LL<<16, 1LL<<16);
    sylvan_init_package();
    sylvan_init_bdd();
    
    g_parallel_state.n_workers = n_workers;
    g_parallel_state.dqsize = dqsize;
    g_parallel_state.initialized = true;
    
    printf("✅ Lace parallel framework initialized with %u workers\n", n_workers);
    printf("✅ Sylvan BDD library initialized\n");
    
    return 0;
}

void ndd_lace_cleanup() {
    if (!g_parallel_state.initialized) {
        return;
    }
    
    printf("🔧 Cleaning up Lace + Sylvan...\n");
    
    // Clean up Sylvan
    sylvan_quit();
    
    // Clean up Lace
    lace_stop();
    
    g_parallel_state.initialized = false;
    printf("✅ Lace + Sylvan parallel framework cleaned up\n");
}

// Real parallel BDD operation wrappers
ndd_bdd_t ndd_bdd_and_parallel(ndd_bdd_t a, ndd_bdd_t b) {
    return sylvan_and(a, b);
}

ndd_bdd_t ndd_bdd_or_parallel(ndd_bdd_t a, ndd_bdd_t b) {
    return sylvan_or(a, b);
}

ndd_bdd_t ndd_bdd_not_parallel(ndd_bdd_t a) {
    return sylvan_not(a);
}

// Check if Lace + Sylvan is available
bool ndd_parallel_available() {
    return g_parallel_state.initialized;
}

#else
#error "NDD requires Lace + Sylvan framework. Please ensure HAVE_LACE_SYLVAN is defined and Lace/Sylvan are properly installed."
#endif

// Generic parallel task management interface
void ndd_spawn_task(void (*task_func)(void*), void *arg) {
#ifdef HAVE_LACE_SYLVAN
    if (g_parallel_state.initialized) {
        // Use real Lace task scheduling
        // TODO: Implement Lace SPAWN macro wrapper
        task_func(arg);
        return;
    }
#endif
    // Execute task directly (serial fallback)
    task_func(arg);
}

ndd_t ndd_sync_task(ndd_t result) {
#ifdef HAVE_LACE_SYLVAN
    if (g_parallel_state.initialized) {
        // Use real Lace synchronization
        // TODO: Implement Lace SYNC macro wrapper
        return result;
    }
#endif
    // Return result directly (serial fallback)
    return result;
}
