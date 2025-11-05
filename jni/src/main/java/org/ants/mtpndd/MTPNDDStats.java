package org.ants.mtpndd;

import java.util.Arrays;

/**
 * Snapshot of the instrumentation counters exported from the native runtime.
 */
public final class MTPNDDStats {
    private static final int IDX_MAX_EDGES_PER_NODE = 0;
    private static final int IDX_BDD_NODES_CONVERTED = 1;
    private static final int IDX_CACHE_HITS = 2;
    private static final int IDX_CACHE_MISSES = 3;
    private static final int IDX_GC_RUNS = 4;
    private static final int IDX_NODES_CREATED = 5;
    private static final int IDX_NODES_REUSED = 6;
    private static final int IDX_NODES_COLLECTED = 7;
    private static final int IDX_EDGE_INSERT = 8;
    private static final int IDX_EDGE_COLLISION = 9;
    private static final int IDX_NODETABLE_COLLISION = 10;
    private static final int IDX_EDGE_LOCK_SPIN = 11;
    private static final int IDX_EDGE_LOCK_WAIT_NS = 12;
    private static final int IDX_GC_PAUSE_NS = 13;
    private static final int IDX_EDGE_ENTRY_TOTAL = 14;
    private static final int IDX_BDD_NODES_PROCESSED = 15;
    private static final int IDX_NODE_POOL_ACQUIRE = 16;
    private static final int IDX_NODE_POOL_RELEASE = 17;
    private static final int IDX_NODE_POOL_SLAB = 18;
    private static final int IDX_EDGE_ENTRY_POOL_ACQUIRE = 19;
    private static final int IDX_EDGE_ENTRY_POOL_RELEASE = 20;
    private static final int IDX_EDGE_ENTRY_POOL_SLAB = 21;
    private static final int IDX_NODETABLE_ENTRY_POOL_ACQUIRE = 22;
    private static final int IDX_NODETABLE_ENTRY_POOL_RELEASE = 23;
    private static final int IDX_NODETABLE_ENTRY_POOL_SLAB = 24;
    private static final int IDX_EDGE_MAP_POOL_ACQUIRE = 25;
    private static final int IDX_EDGE_MAP_POOL_RELEASE = 26;
    private static final int IDX_EDGE_MAP_POOL_SLAB = 27;

    private final long nodeCount;
    private final boolean recordingEnabled;
    private final long[] metrics;

    public MTPNDDStats(long nodeCount, boolean recordingEnabled, long[] metrics) {
        this.nodeCount = nodeCount;
        this.recordingEnabled = recordingEnabled;
        this.metrics = metrics != null ? metrics.clone() : new long[0];
    }

    public long nodeCount() {
        return nodeCount;
    }

    public boolean recordingEnabled() {
        return recordingEnabled;
    }

    public long maxEdgesPerNode() {
        return metric(IDX_MAX_EDGES_PER_NODE);
    }

    public long nodesCreatedTotal() {
        return metric(IDX_NODES_CREATED);
    }

    public long nodesReusedTotal() {
        return metric(IDX_NODES_REUSED);
    }

    public long nodesCollectedLast() {
        return metric(IDX_NODES_COLLECTED);
    }

    public long cacheHits() {
        return metric(IDX_CACHE_HITS);
    }

    public long cacheMisses() {
        return metric(IDX_CACHE_MISSES);
    }

    public long nodePoolAcquireTotal() {
        return metric(IDX_NODE_POOL_ACQUIRE);
    }

    public long edgeEntryPoolAcquireTotal() {
        return metric(IDX_EDGE_ENTRY_POOL_ACQUIRE);
    }

    public long nodetableEntryPoolAcquireTotal() {
        return metric(IDX_NODETABLE_ENTRY_POOL_ACQUIRE);
    }

    public long edgeMapPoolAcquireTotal() {
        return metric(IDX_EDGE_MAP_POOL_ACQUIRE);
    }

    public long[] rawMetrics() {
        return metrics.clone();
    }

    private long metric(int index) {
        if (!recordingEnabled || index < 0 || index >= metrics.length) {
            return 0L;
        }
        return metrics[index];
    }

    @Override
    public String toString() {
        return "MTPNDDStats{" +
                "nodeCount=" + nodeCount +
                ", recordingEnabled=" + recordingEnabled +
                ", metrics=" + Arrays.toString(metrics) +
                '}';
    }
}
