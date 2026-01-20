/*
 * MTPNDD Edge Statistics Module - Implementation
 *
 * Provides file output functionality for BDD operation edge statistics.
 */

#include "mtpndd_edge_stats.h"

#ifdef ENABLE_RECORDING
#ifdef MTPNDD_ENABLE_EDGE_STATS_FILE

#include <stdio.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

// Module-private file handles
static FILE *g_mtpndd_and_edges_file = NULL;
static FILE *g_mtpndd_or_edges_file = NULL;
static FILE *g_mtpndd_diff_edges_file = NULL;

void mtpndd_edge_stats_open(const char *output_dir) {
    // Create output directory if it doesn't exist
    if (output_dir != NULL && output_dir[0] != '\0') {
        mkdir(output_dir, 0755);
    }

    char filepath[512];
    const char *dir = (output_dir != NULL && output_dir[0] != '\0') ? output_dir : ".";

    // Open AND edges file
    snprintf(filepath, sizeof(filepath), "%s/and_edges.txt", dir);
    g_mtpndd_and_edges_file = fopen(filepath, "w");
    if (g_mtpndd_and_edges_file != NULL) {
        fprintf(g_mtpndd_and_edges_file, "# AND operation edge counts: edges_a,edges_b\n");
        setvbuf(g_mtpndd_and_edges_file, NULL, _IOFBF, 65536); // 64KB buffer
    }

    // Open OR edges file
    snprintf(filepath, sizeof(filepath), "%s/or_edges.txt", dir);
    g_mtpndd_or_edges_file = fopen(filepath, "w");
    if (g_mtpndd_or_edges_file != NULL) {
        fprintf(g_mtpndd_or_edges_file, "# OR operation edge counts: edges_a,edges_b\n");
        setvbuf(g_mtpndd_or_edges_file, NULL, _IOFBF, 65536); // 64KB buffer
    }

    // Open DIFF edges file
    snprintf(filepath, sizeof(filepath), "%s/diff_edges.txt", dir);
    g_mtpndd_diff_edges_file = fopen(filepath, "w");
    if (g_mtpndd_diff_edges_file != NULL) {
        fprintf(g_mtpndd_diff_edges_file, "# DIFF operation edge counts: edges_a,edges_b\n");
        setvbuf(g_mtpndd_diff_edges_file, NULL, _IOFBF, 65536); // 64KB buffer
    }
}

void mtpndd_edge_stats_close(void) {
    if (g_mtpndd_and_edges_file != NULL) {
        fclose(g_mtpndd_and_edges_file);
        g_mtpndd_and_edges_file = NULL;
    }
    if (g_mtpndd_or_edges_file != NULL) {
        fclose(g_mtpndd_or_edges_file);
        g_mtpndd_or_edges_file = NULL;
    }
    if (g_mtpndd_diff_edges_file != NULL) {
        fclose(g_mtpndd_diff_edges_file);
        g_mtpndd_diff_edges_file = NULL;
    }
}

void mtpndd_edge_stats_write_and(uint64_t edges_a, uint64_t edges_b) {
    if (g_mtpndd_and_edges_file != NULL) {
        fprintf(g_mtpndd_and_edges_file, "%" PRIu64 ",%" PRIu64 "\n", edges_a, edges_b);
    }
}

void mtpndd_edge_stats_write_or(uint64_t edges_a, uint64_t edges_b) {
    if (g_mtpndd_or_edges_file != NULL) {
        fprintf(g_mtpndd_or_edges_file, "%" PRIu64 ",%" PRIu64 "\n", edges_a, edges_b);
    }
}

void mtpndd_edge_stats_write_diff(uint64_t edges_a, uint64_t edges_b) {
    if (g_mtpndd_diff_edges_file != NULL) {
        fprintf(g_mtpndd_diff_edges_file, "%" PRIu64 ",%" PRIu64 "\n", edges_a, edges_b);
    }
}

#endif  // MTPNDD_ENABLE_EDGE_STATS_FILE
#endif  // ENABLE_RECORDING
