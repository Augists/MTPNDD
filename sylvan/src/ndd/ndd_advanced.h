// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef NDD_ADVANCED_H
#define NDD_ADVANCED_H

#include "ndd.h"

// Advanced NDD operation declarations

// Constraint propagation operations
typedef struct ndd_constraint_s {
    uint32_t field;
    ndd_bdd_t constraint_bdd;
    bool is_equality;     // Whether it's an equality constraint
    bool is_range;        // Whether it's a range constraint
    uint32_t min_value;   // Minimum value for range constraint
    uint32_t max_value;   // Maximum value for range constraint
} ndd_constraint_t;

// Constraint propagation functions
ndd_error_t ndd_apply_constraint_safe(ndd_t ndd, ndd_constraint_t constraint, ndd_t *result);
ndd_t ndd_apply_constraint(ndd_t ndd, ndd_constraint_t constraint);
ndd_error_t ndd_propagate_constraints_safe(ndd_t ndd, ndd_constraint_t *constraints, 
                                          uint32_t constraint_count, ndd_t *result);
ndd_t ndd_propagate_constraints(ndd_t ndd, ndd_constraint_t *constraints, uint32_t constraint_count);

// Node merging and optimization
ndd_error_t ndd_merge_nodes_safe(ndd_t a, ndd_t b, ndd_t *result);
ndd_t ndd_merge_nodes(ndd_t a, ndd_t b);
ndd_error_t ndd_reduce_safe(ndd_t ndd, ndd_t *result);
ndd_t ndd_reduce(ndd_t ndd);
ndd_error_t ndd_simplify_safe(ndd_t ndd, ndd_t *result);
ndd_t ndd_simplify(ndd_t ndd);

// Advanced logic operations
ndd_error_t ndd_ite_safe(ndd_t condition, ndd_t then_branch, ndd_t else_branch, ndd_t *result);
ndd_t ndd_ite(ndd_t condition, ndd_t then_branch, ndd_t else_branch);  // if-then-else
ndd_error_t ndd_compose_safe(ndd_t ndd, uint32_t field, ndd_t replacement, ndd_t *result);
ndd_t ndd_compose(ndd_t ndd, uint32_t field, ndd_t replacement);  // Variable substitution

// Quantification operations
ndd_error_t ndd_forall_safe(ndd_t ndd, uint32_t field, ndd_t *result);
ndd_t ndd_forall(ndd_t ndd, uint32_t field);  // Universal quantification
ndd_error_t ndd_exists_safe(ndd_t ndd, uint32_t field, ndd_t *result);
ndd_t ndd_exists(ndd_t ndd, uint32_t field);  // Existential quantification

// Satisfiability checking and counting
ndd_error_t ndd_is_satisfiable_safe(ndd_t ndd, bool *result);
bool ndd_is_satisfiable(ndd_t ndd);
ndd_error_t ndd_sat_count_safe(ndd_t ndd, uint64_t *result);
uint64_t ndd_sat_count(ndd_t ndd);
ndd_error_t ndd_sat_one_safe(ndd_t ndd, ndd_t *result);
ndd_t ndd_sat_one(ndd_t ndd);  // Find one satisfying solution

// Graph analysis operations
ndd_error_t ndd_node_count_safe(ndd_t ndd, uint64_t *result);
uint64_t ndd_node_count(ndd_t ndd);
ndd_error_t ndd_depth_safe(ndd_t ndd, uint32_t *result);
uint32_t ndd_depth(ndd_t ndd);
ndd_error_t ndd_support_safe(ndd_t ndd, uint32_t **fields, uint32_t *field_count);
uint32_t* ndd_support(ndd_t ndd, uint32_t *field_count);  // Support set

// Graph transformation operations
ndd_error_t ndd_cofactor_safe(ndd_t ndd, uint32_t field, uint32_t value, ndd_t *result);
ndd_t ndd_cofactor(ndd_t ndd, uint32_t field, uint32_t value);  // Cofactor
ndd_error_t ndd_restrict_safe(ndd_t ndd, ndd_t restriction, ndd_t *result);
ndd_t ndd_restrict(ndd_t ndd, ndd_t restriction);  // Restriction operation

// Equivalence checking
ndd_error_t ndd_is_equal_safe(ndd_t a, ndd_t b, bool *result);
bool ndd_is_equal(ndd_t a, ndd_t b);
ndd_error_t ndd_is_subset_safe(ndd_t a, ndd_t b, bool *result);
bool ndd_is_subset(ndd_t a, ndd_t b);

// Advanced performance optimization
typedef struct ndd_optimization_config_s {
    bool enable_dynamic_reordering;
    bool enable_aggressive_gc;
    bool enable_node_sharing;
    uint32_t merge_threshold;
    double cache_hit_ratio_target;
} ndd_optimization_config_t;

ndd_error_t ndd_set_optimization_config_safe(ndd_optimization_config_t config);
void ndd_set_optimization_config(ndd_optimization_config_t config);
ndd_optimization_config_t ndd_get_optimization_config();

// Constraint solver interface
typedef struct ndd_solver_s {
    ndd_t problem;
    ndd_constraint_t *constraints;
    uint32_t constraint_count;
    uint32_t max_solutions;
    bool find_all_solutions;
} ndd_solver_t;

ndd_error_t ndd_solver_create_safe(ndd_t problem, ndd_solver_t **solver);
ndd_solver_t* ndd_solver_create(ndd_t problem);
ndd_error_t ndd_solver_add_constraint_safe(ndd_solver_t *solver, ndd_constraint_t constraint);
void ndd_solver_add_constraint(ndd_solver_t *solver, ndd_constraint_t constraint);
ndd_error_t ndd_solver_solve_safe(ndd_solver_t *solver, ndd_t **solutions, uint32_t *solution_count);
ndd_t* ndd_solver_solve(ndd_solver_t *solver, uint32_t *solution_count);
void ndd_solver_destroy(ndd_solver_t *solver);

#endif // NDD_ADVANCED_H