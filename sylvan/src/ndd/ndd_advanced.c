// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd_advanced.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Helper: add an edge to edges_map (OR merge labels if same descendant)
static void edges_map_add(hash_table_t *map, ndd_t descendant, ndd_bdd_t label)
{
    ndd_bdd_t *old = (ndd_bdd_t*)hash_table_get(map, descendant);
    if (old) {
        *old = ndd_bdd_or_parallel(*old, label);
    } else {
        ndd_bdd_t *stored = (ndd_bdd_t*)malloc(sizeof(ndd_bdd_t));
        if (!stored) return;
        *stored = label;
        hash_table_put(map, descendant, stored);
    }
}

// Global optimization configuration
static ndd_optimization_config_t g_optimization_config = {
    .enable_dynamic_reordering = false,
    .enable_aggressive_gc = false,
    .enable_node_sharing = true,
    .merge_threshold = 100,
    .cache_hit_ratio_target = 0.8
};

// ===== Constraint propagation operations =====

ndd_error_t ndd_apply_constraint_safe(ndd_t ndd, ndd_constraint_t constraint, ndd_t *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    if (ndd_is_terminal(ndd)) {
        *result = ndd_ref_safe(ndd);
        return NDD_SUCCESS;
    }
    
    // Check if field matches
    if (ndd->field != constraint.field) {
        // Field doesn't match, return original NDD directly
        *result = ndd_ref_safe(ndd);
        return NDD_SUCCESS;
    }
    
    // Apply constraint to current field
    ndd_t constrained_result = ndd_create_node(constraint.field);
    
    if (constraint.is_equality) {
        // Equality constraint: only keep edges that satisfy the constraint
        for (size_t b = 0; b < ndd->edges_map->bucket_count; ++b) {
            hash_entry_t *e = ndd->edges_map->buckets[b];
            while (e) {
                ndd_bdd_t edge_bdd = *(ndd_bdd_t*)e->value;
                // Perform AND operation between edge BDD and constraint BDD
                ndd_bdd_t constrained_edge = ndd_bdd_and_parallel(edge_bdd, constraint.constraint_bdd);
                if (constrained_edge != ndd_sylvan_false) {
                    ndd_add_edge(&constrained_result, ndd_true(), constrained_edge);
                }
                e = e->next;
            }
        }
    } else if (constraint.is_range) {
        // Range constraint: keep edges within range (simplified implementation)
        for (size_t b = 0; b < ndd->edges_map->bucket_count; ++b) {
            hash_entry_t *e = ndd->edges_map->buckets[b];
            while (e) {
                ndd_bdd_t edge_bdd = *(ndd_bdd_t*)e->value;
                ndd_add_edge(&constrained_result, ndd_true(), edge_bdd);
                e = e->next;
            }
        }
    } else {
        // General constraint: use BDD AND operation
        for (size_t b = 0; b < ndd->edges_map->bucket_count; ++b) {
            hash_entry_t *e = ndd->edges_map->buckets[b];
            while (e) {
                ndd_bdd_t edge_bdd = *(ndd_bdd_t*)e->value;
                ndd_bdd_t constrained_edge = ndd_bdd_and_parallel(edge_bdd, constraint.constraint_bdd);
                if (constrained_edge != ndd_sylvan_false) {
                    ndd_add_edge(&constrained_result, ndd_true(), constrained_edge);
                }
                e = e->next;
            }
        }
    }
    
    *result = constrained_result;
    return NDD_SUCCESS;
}

ndd_t ndd_apply_constraint(ndd_t ndd, ndd_constraint_t constraint) {
    ndd_t result;
    ndd_error_t error = ndd_apply_constraint_safe(ndd, constraint, &result);
    if (error != NDD_SUCCESS) {
        return ndd_false();
    }
    return result;
}

ndd_error_t ndd_propagate_constraints_safe(ndd_t ndd, ndd_constraint_t *constraints, 
                                          uint32_t constraint_count, ndd_t *result) {
    NDD_CHECK_NULL(constraints, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    ndd_t current = ndd_ref_safe(ndd);
    
    // Apply constraints one by one
    for (uint32_t i = 0; i < constraint_count; i++) {
        ndd_t temp_result;
        ndd_error_t error = ndd_apply_constraint_safe(current, constraints[i], &temp_result);
        
        ndd_deref_safe(current);  // Release old result
        
        if (error != NDD_SUCCESS) {
            NDD_RETURN_ERROR(error);
        }
        
        current = temp_result;
        
        // If result is false, terminate early
        if (ndd_is_false(current)) {
            break;
        }
    }
    
    *result = current;
    return NDD_SUCCESS;
}

ndd_t ndd_propagate_constraints(ndd_t ndd, ndd_constraint_t *constraints, uint32_t constraint_count) {
    ndd_t result;
    ndd_error_t error = ndd_propagate_constraints_safe(ndd, constraints, constraint_count, &result);
    if (error != NDD_SUCCESS) {
        return ndd_false();
    }
    return result;
}

// ===== Node merging and optimization =====

ndd_error_t ndd_merge_nodes_safe(ndd_t a, ndd_t b, ndd_t *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    // Terminal case
    if (ndd_is_terminal(a) && ndd_is_terminal(b)) {
        if (ndd_is_true(a) || ndd_is_true(b)) {
            *result = ndd_true();
        } else {
            *result = ndd_false();
        }
        return NDD_SUCCESS;
    }
    
    if (ndd_is_terminal(a)) {
        *result = ndd_ref_safe(b);
        return NDD_SUCCESS;
    }
    
    if (ndd_is_terminal(b)) {
        *result = ndd_ref_safe(a);
        return NDD_SUCCESS;
    }
    
    // Choose smaller field as top-level field for merging
    uint32_t merge_field = (a->field < b->field) ? a->field : b->field;
    
    // Merge edges
    if (a->field == b->field) {
        // Same field: merge corresponding edges
        
        // Use edges_map to merge edges
        hash_table_t *emap = hash_table_create(32, ptr_hash, ptr_compare);
        if (!emap) {
            *result = ndd_false();
            return NDD_ERROR_OUT_OF_MEMORY;
        }
        
        // Add edges from a
        for (size_t b = 0; b < a->edges_map->bucket_count; ++b) {
            hash_entry_t *e = a->edges_map->buckets[b];
            while (e) {
                edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)e->value);
                e = e->next;
            }
        }
        
        // Add edges from b
        for (size_t bb = 0; bb < b->edges_map->bucket_count; ++bb) {
            hash_entry_t *e = b->edges_map->buckets[bb];
            while (e) {
                edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)e->value);
                e = e->next;
            }
        }
        
        *result = ndd_mk(a->field, emap);
    } else if (a->field < b->field) {
        // a field is smaller: copy a's edges, add b as a branch
        hash_table_t *emap = hash_table_create(32, ptr_hash, ptr_compare);
        if (!emap) {
            *result = ndd_false();
            return NDD_ERROR_OUT_OF_MEMORY;
        }
        for (size_t ba = 0; ba < a->edges_map->bucket_count; ++ba) {
            hash_entry_t *e = a->edges_map->buckets[ba];
            while (e) {
                edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)e->value);
                e = e->next;
            }
        }
        edges_map_add(emap, b, ndd_sylvan_true);
        *result = ndd_mk(a->field, emap);
    } else {
        // b field is smaller: copy b's edges, add a as a branch
        hash_table_t *emap = hash_table_create(32, ptr_hash, ptr_compare);
        if (!emap) {
            *result = ndd_false();
            return NDD_ERROR_OUT_OF_MEMORY;
        }
        for (size_t bb = 0; bb < b->edges_map->bucket_count; ++bb) {
            hash_entry_t *e = b->edges_map->buckets[bb];
            while (e) {
                edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)e->value);
                e = e->next;
            }
        }
        edges_map_add(emap, a, ndd_sylvan_true);
        *result = ndd_mk(b->field, emap);
    }
    
    return NDD_SUCCESS;
}

ndd_t ndd_merge_nodes(ndd_t a, ndd_t b) {
    ndd_t result;
    ndd_error_t error = ndd_merge_nodes_safe(a, b, &result);
    if (error != NDD_SUCCESS) {
        return ndd_false();
    }
    return result;
}

ndd_error_t ndd_reduce_safe(ndd_t ndd, ndd_t *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    if (ndd_is_terminal(ndd)) {
        *result = ndd_ref_safe(ndd);
        return NDD_SUCCESS;
    }
    
    // Simplified: remove redundant edges
    
    // edges_map automatically deduplicates, copy directly
    hash_table_t *emap = hash_table_create(32, ptr_hash, ptr_compare);
    if (!emap) {
        *result = ndd_false();
        return NDD_ERROR_OUT_OF_MEMORY;
    }
    for (size_t b = 0; b < ndd->edges_map->bucket_count; ++b) {
        hash_entry_t *e = ndd->edges_map->buckets[b];
        while (e) {
            edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)e->value);
            e = e->next;
        }
    }
    *result = ndd_mk(ndd->field, emap);
    
    return NDD_SUCCESS;
}

ndd_t ndd_reduce(ndd_t ndd) {
    ndd_t result;
    ndd_error_t error = ndd_reduce_safe(ndd, &result);
    if (error != NDD_SUCCESS) {
        return ndd_false();
    }
    return result;
}

// ===== Advanced Logic Operations =====

ndd_error_t ndd_ite_safe(ndd_t condition, ndd_t then_branch, ndd_t else_branch, ndd_t *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    // if-then-else: (condition AND then_branch) OR (NOT condition AND else_branch)
    ndd_t not_condition = ndd_not(condition);
    ndd_t then_part = ndd_and(condition, then_branch);
    ndd_t else_part = ndd_and(not_condition, else_branch);
    ndd_t ite_result = ndd_or(then_part, else_part);
    
    // Clean up temporary results
    ndd_deref_safe(not_condition);
    ndd_deref_safe(then_part);
    ndd_deref_safe(else_part);
    
    *result = ite_result;
    return NDD_SUCCESS;
}

ndd_t ndd_ite(ndd_t condition, ndd_t then_branch, ndd_t else_branch) {
    ndd_t result;
    ndd_error_t error = ndd_ite_safe(condition, then_branch, else_branch, &result);
    if (error != NDD_SUCCESS) {
        return ndd_false();
    }
    return result;
}

// ===== Satisfiability checking =====

ndd_error_t ndd_is_satisfiable_safe(ndd_t ndd, bool *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    *result = !ndd_is_false(ndd);
    return NDD_SUCCESS;
}

bool ndd_is_satisfiable(ndd_t ndd) {
    bool result;
    ndd_error_t error = ndd_is_satisfiable_safe(ndd, &result);
    if (error != NDD_SUCCESS) {
        return false;
    }
    return result;
}

ndd_error_t ndd_sat_count_safe(ndd_t ndd, uint64_t *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    if (ndd_is_false(ndd)) {
        *result = 0;
        return NDD_SUCCESS;
    }
    
    if (ndd_is_true(ndd)) {
        *result = 1;
        return NDD_SUCCESS;
    }
    
    // Simplified implementation: recursive calculation
    // Actual implementation needs to consider caching and more complex algorithms
    uint64_t count = 0;
    
    if (ndd->edge_count > 0) {
        count = ndd->edge_count;  // Simplified calculation
    }
    
    *result = count;
    return NDD_SUCCESS;
}

uint64_t ndd_sat_count(ndd_t ndd) {
    uint64_t result;
    ndd_error_t error = ndd_sat_count_safe(ndd, &result);
    if (error != NDD_SUCCESS) {
        return 0;
    }
    return result;
}

// ===== Equivalence Checking =====

ndd_error_t ndd_is_equal_safe(ndd_t a, ndd_t b, bool *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    // Simplified check: compare node pointers
    *result = (a == b);
    return NDD_SUCCESS;
}

bool ndd_is_equal(ndd_t a, ndd_t b) {
    bool result;
    ndd_error_t error = ndd_is_equal_safe(a, b, &result);
    if (error != NDD_SUCCESS) {
        return false;
    }
    return result;
}

// ===== Optimization Configuration =====

ndd_error_t ndd_set_optimization_config_safe(ndd_optimization_config_t config) {
    NDD_CHECK_INIT();
    
    g_optimization_config = config;
    return NDD_SUCCESS;
}

void ndd_set_optimization_config(ndd_optimization_config_t config) {
    g_optimization_config = config;
}

ndd_optimization_config_t ndd_get_optimization_config() {
    return g_optimization_config;
}

// ===== Constraint solver =====

ndd_error_t ndd_solver_create_safe(ndd_t problem, ndd_solver_t **solver) {
    NDD_CHECK_NULL(solver, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    ndd_solver_t *new_solver = (ndd_solver_t*)malloc(sizeof(ndd_solver_t));
    if (!new_solver) {
        NDD_RETURN_ERROR(NDD_ERROR_OUT_OF_MEMORY);
    }
    
    new_solver->problem = ndd_ref_safe(problem);
    new_solver->constraints = NULL;
    new_solver->constraint_count = 0;
    new_solver->max_solutions = 10;
    new_solver->find_all_solutions = false;
    
    *solver = new_solver;
    return NDD_SUCCESS;
}

ndd_solver_t* ndd_solver_create(ndd_t problem) {
    ndd_solver_t *solver;
    ndd_error_t error = ndd_solver_create_safe(problem, &solver);
    if (error != NDD_SUCCESS) {
        return NULL;
    }
    return solver;
}

ndd_error_t ndd_solver_add_constraint_safe(ndd_solver_t *solver, ndd_constraint_t constraint) {
    NDD_CHECK_NULL(solver, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    // Reallocate constraint array
    ndd_constraint_t *new_constraints = (ndd_constraint_t*)realloc(
        solver->constraints, 
        (solver->constraint_count + 1) * sizeof(ndd_constraint_t));
    
    if (!new_constraints) {
        NDD_RETURN_ERROR(NDD_ERROR_OUT_OF_MEMORY);
    }
    
    solver->constraints = new_constraints;
    solver->constraints[solver->constraint_count] = constraint;
    solver->constraint_count++;
    
    return NDD_SUCCESS;
}

void ndd_solver_add_constraint(ndd_solver_t *solver, ndd_constraint_t constraint) {
    ndd_solver_add_constraint_safe(solver, constraint);
}

void ndd_solver_destroy(ndd_solver_t *solver) {
    if (solver) {
        ndd_deref_safe(solver->problem);
        free(solver->constraints);
        free(solver);
    }
}