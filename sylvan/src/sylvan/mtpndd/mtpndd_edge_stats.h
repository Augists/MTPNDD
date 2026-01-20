/*
 * MTPNDD Edge Statistics Module
 *
 * This module provides optional file output for edge statistics during BDD operations.
 * It can be enabled/disabled independently from the main statistics collection.
 *
 * Usage:
 *   1. Enable both ENABLE_RECORDING and MTPNDD_ENABLE_EDGE_STATS_FILE in CMake
 *   2. Call mtpndd_edge_stats_open() with output directory path
 *   3. Operations automatically write edge counts to files
 *   4. Call mtpndd_edge_stats_close() when done
 *
 * When disabled, all functions compile to zero-cost no-ops.
 */

#ifndef MTPNDD_EDGE_STATS_H
#define MTPNDD_EDGE_STATS_H

#include <stdint.h>

#ifdef ENABLE_RECORDING
#ifdef MTPNDD_ENABLE_EDGE_STATS_FILE

/*
 * Open edge statistics output files in the specified directory.
 * Creates three files: and_edges.txt, or_edges.txt, diff_edges.txt
 *
 * @param output_dir Directory path for output files (NULL or empty string uses ".")
 */
void mtpndd_edge_stats_open(const char *output_dir);

/*
 * Close all edge statistics output files.
 */
void mtpndd_edge_stats_close(void);

/*
 * Write edge counts for an AND operation.
 *
 * @param edges_a Number of edges in first operand
 * @param edges_b Number of edges in second operand
 */
void mtpndd_edge_stats_write_and(uint64_t edges_a, uint64_t edges_b);

/*
 * Write edge counts for an OR operation.
 *
 * @param edges_a Number of edges in first operand
 * @param edges_b Number of edges in second operand
 */
void mtpndd_edge_stats_write_or(uint64_t edges_a, uint64_t edges_b);

/*
 * Write edge counts for a DIFF operation.
 *
 * @param edges_a Number of edges in first operand
 * @param edges_b Number of edges in second operand
 */
void mtpndd_edge_stats_write_diff(uint64_t edges_a, uint64_t edges_b);

#else  // MTPNDD_ENABLE_EDGE_STATS_FILE not defined

// No-op implementations when file output is disabled
static inline void mtpndd_edge_stats_open(const char *output_dir) {
    (void)output_dir;
}

static inline void mtpndd_edge_stats_close(void) {
}

static inline void mtpndd_edge_stats_write_and(uint64_t edges_a, uint64_t edges_b) {
    (void)edges_a;
    (void)edges_b;
}

static inline void mtpndd_edge_stats_write_or(uint64_t edges_a, uint64_t edges_b) {
    (void)edges_a;
    (void)edges_b;
}

static inline void mtpndd_edge_stats_write_diff(uint64_t edges_a, uint64_t edges_b) {
    (void)edges_a;
    (void)edges_b;
}

#endif  // MTPNDD_ENABLE_EDGE_STATS_FILE
#else   // ENABLE_RECORDING not defined

// No-op implementations when recording is completely disabled
static inline void mtpndd_edge_stats_open(const char *output_dir) {
    (void)output_dir;
}

static inline void mtpndd_edge_stats_close(void) {
}

static inline void mtpndd_edge_stats_write_and(uint64_t edges_a, uint64_t edges_b) {
    (void)edges_a;
    (void)edges_b;
}

static inline void mtpndd_edge_stats_write_or(uint64_t edges_a, uint64_t edges_b) {
    (void)edges_a;
    (void)edges_b;
}

static inline void mtpndd_edge_stats_write_diff(uint64_t edges_a, uint64_t edges_b) {
    (void)edges_a;
    (void)edges_b;
}

#endif  // ENABLE_RECORDING

#endif  // MTPNDD_EDGE_STATS_H
