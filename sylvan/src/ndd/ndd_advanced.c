// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd_advanced.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 全局优化配置
static ndd_optimization_config_t g_optimization_config = {
    .enable_dynamic_reordering = false,
    .enable_aggressive_gc = false,
    .enable_node_sharing = true,
    .merge_threshold = 100,
    .cache_hit_ratio_target = 0.8
};

// ===== 约束传播操作 =====

ndd_error_t ndd_apply_constraint_safe(ndd_t ndd, ndd_constraint_t constraint, ndd_t *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    if (ndd_is_terminal(ndd)) {
        *result = ndd_ref_safe(ndd);
        return NDD_SUCCESS;
    }
    
    // 检查字段是否匹配
    if (ndd.node->field != constraint.field) {
        // 字段不匹配，直接返回原NDD
        *result = ndd_ref_safe(ndd);
        return NDD_SUCCESS;
    }
    
    // 应用约束到当前字段
    ndd_t constrained_result = ndd_create_node(constraint.field);
    
    if (constraint.is_equality) {
        // 等值约束：只保留满足约束的边
        for (uint32_t i = 0; i < ndd.node->edge_count; i++) {
            ndd_bdd_t edge_bdd = ndd.node->edges[i];
            // 将边BDD与约束BDD进行AND操作
            ndd_bdd_t constrained_edge = ndd_bdd_and_parallel(edge_bdd, constraint.constraint_bdd);
            if (constrained_edge != ndd_sylvan_false) {
                ndd_add_edge(&constrained_result, ndd_true(), constrained_edge);
            }
        }
    } else if (constraint.is_range) {
        // 范围约束：保留在范围内的边
        for (uint32_t i = 0; i < ndd.node->edge_count; i++) {
            // 简化实现：假设边的索引对应值
            if (i >= constraint.min_value && i <= constraint.max_value) {
                ndd_add_edge(&constrained_result, ndd_true(), ndd.node->edges[i]);
            }
        }
    } else {
        // 一般约束：使用BDD AND操作
        for (uint32_t i = 0; i < ndd.node->edge_count; i++) {
            ndd_bdd_t edge_bdd = ndd.node->edges[i];
            ndd_bdd_t constrained_edge = ndd_bdd_and_parallel(edge_bdd, constraint.constraint_bdd);
            if (constrained_edge != ndd_sylvan_false) {
                ndd_add_edge(&constrained_result, ndd_true(), constrained_edge);
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
    
    // 逐个应用约束
    for (uint32_t i = 0; i < constraint_count; i++) {
        ndd_t temp_result;
        ndd_error_t error = ndd_apply_constraint_safe(current, constraints[i], &temp_result);
        
        ndd_deref_safe(current);  // 释放旧结果
        
        if (error != NDD_SUCCESS) {
            NDD_RETURN_ERROR(error);
        }
        
        current = temp_result;
        
        // 如果结果为false，提前终止
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

// ===== 节点合并和优化 =====

ndd_error_t ndd_merge_nodes_safe(ndd_t a, ndd_t b, ndd_t *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    // 终端情况
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
    
    // 选择较小字段作为合并的顶层字段
    uint32_t merge_field = (a.node->field < b.node->field) ? a.node->field : b.node->field;
    ndd_t merged = ndd_create_node(merge_field);
    
    // 合并边
    if (a.node->field == b.node->field) {
        // 同一字段：合并对应的边
        uint32_t max_edges = (a.node->edge_count > b.node->edge_count) ? 
                           a.node->edge_count : b.node->edge_count;
        
        for (uint32_t i = 0; i < max_edges; i++) {
            ndd_bdd_t edge_a = (i < a.node->edge_count) ? a.node->edges[i] : ndd_sylvan_false;
            ndd_bdd_t edge_b = (i < b.node->edge_count) ? b.node->edges[i] : ndd_sylvan_false;
            
            ndd_bdd_t merged_edge = ndd_bdd_or_parallel(edge_a, edge_b);
            if (merged_edge != ndd_sylvan_false) {
                ndd_add_edge(&merged, ndd_true(), merged_edge);
            }
        }
    } else if (a.node->field < b.node->field) {
        // a字段更小：复制a的边，添加b作为一个分支
        for (uint32_t i = 0; i < a.node->edge_count; i++) {
            ndd_add_edge(&merged, ndd_true(), a.node->edges[i]);
        }
        ndd_add_edge(&merged, b, ndd_sylvan_true);
    } else {
        // b字段更小：复制b的边，添加a作为一个分支
        for (uint32_t i = 0; i < b.node->edge_count; i++) {
            ndd_add_edge(&merged, ndd_true(), b.node->edges[i]);
        }
        ndd_add_edge(&merged, a, ndd_sylvan_true);
    }
    
    *result = merged;
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
    
    // 简化：移除冗余边
    ndd_t reduced = ndd_create_node(ndd.node->field);
    
    for (uint32_t i = 0; i < ndd.node->edge_count; i++) {
        ndd_bdd_t edge = ndd.node->edges[i];
        if (edge != ndd_sylvan_false) {
            // 检查是否已存在相同的边
            bool duplicate = false;
            for (uint32_t j = 0; j < i; j++) {
                if (ndd.node->edges[j] == edge) {
                    duplicate = true;
                    break;
                }
            }
            
            if (!duplicate) {
                ndd_add_edge(&reduced, ndd_true(), edge);
            }
        }
    }
    
    *result = reduced;
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

// ===== 高级逻辑操作 =====

ndd_error_t ndd_ite_safe(ndd_t condition, ndd_t then_branch, ndd_t else_branch, ndd_t *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    // if-then-else: (condition AND then_branch) OR (NOT condition AND else_branch)
    ndd_t not_condition = ndd_not(condition);
    ndd_t then_part = ndd_and(condition, then_branch);
    ndd_t else_part = ndd_and(not_condition, else_branch);
    ndd_t ite_result = ndd_or(then_part, else_part);
    
    // 清理临时结果
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

// ===== 满足性检查 =====

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
    
    // 简化实现：递归计算
    // 实际实现需要考虑缓存和更复杂的算法
    uint64_t count = 0;
    
    if (ndd.node && ndd.node->edge_count > 0) {
        count = ndd.node->edge_count;  // 简化计算
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

// ===== 等价性检查 =====

ndd_error_t ndd_is_equal_safe(ndd_t a, ndd_t b, bool *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    // 简化检查：比较节点指针
    *result = (a.node == b.node && a.is_terminal == b.is_terminal);
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

// ===== 优化配置 =====

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

// ===== 约束求解器 =====

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
    
    // 重新分配约束数组
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