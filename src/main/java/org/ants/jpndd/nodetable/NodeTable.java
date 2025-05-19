/**
 * Node table of NDD.
 * @author Zechun Li - XJTU ANTS NetVerify Lab
 * @version 1.0
 */
package org.ants.jpndd.nodetable;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.Map;
import java.util.Queue;
import java.util.concurrent.ConcurrentHashMap;

import org.ants.jpndd.diagram.NDD;

import jsylvan.JSylvan;

public class NodeTable {
    /**
     * The current size of the node table.
     */
    long currentSize;

    /**
     * The max size of the node table.
     */
    long nddTableSize;

    /**
     * The node table.
     */
    ArrayList<ConcurrentHashMap<ConcurrentHashMap<NDD, Long>, NDD>> nodeTable;

    /**
     * If the number of free nodes is less than this threshold after garbage
     * collection, the ndd engine will grow its node table.
     */
    final double QUICK_GROW_THRESHOLD = 0.1;

    /**
     * The reference count of each node.
     */
    HashMap<NDD, Integer> referenceCount;

    /**
     * Construct function for ndd.
     * 
     * @param nddTableSize The max size of ndd node table.
     * @param bddTableSize The max size of bdd node table.
     * @param bddCacheSize The max size of ndd operation cache.
     */
    public NodeTable(int nddTableSize, int bddTableSize, int bddCacheSize) {
        this.currentSize = 0L;
        this.nddTableSize = nddTableSize;
        this.nodeTable = new ArrayList<>();

        long maxMemory = 400L * 1024 * 1024;
        // int tableRatio = Math.max(1, bddTableSize / bddCacheSize - 1);
        // int initratio = (int)Math.sqrt((double) maxMemory / (bddTableSize + bddCacheSize));
        try{
            JSylvan.init(2, maxMemory, 1, 4, 1);
        }catch (IOException ex) {
            ex.printStackTrace();
            return;
        }
        JSylvan.disableGC();
        JSylvan.enableGC();
        
        this.referenceCount = new HashMap<>();
    }

    /**
     * Construct function for atomized ndd.
     * 
     * @param nddTableSize The max size of ndd node table.
     * @param bddEngine    The engine for bdd.
     */
    public NodeTable(long nddTableSize, JSylvan bddEngine) {
        this.currentSize = 0L;
        this.nddTableSize = nddTableSize;
        this.nodeTable = new ArrayList<>();
        this.referenceCount = new HashMap<>();
    }

    public ArrayList<ConcurrentHashMap<ConcurrentHashMap<NDD, Long>, NDD>> getNodeTable() {
        return nodeTable;
    }

    /**
     * Declare a new field.
     */
    // declare a new node table for a new field
    public void declareField() {
        nodeTable.add(new ConcurrentHashMap<>());
    }

    /**
     * Create or reuse an ndd node.
     * 
     * @param field The field of the node.
     * @param edges Edges of the node.
     * @return The ndd node.
     */
    // create or reuse a new node
    public NDD mk(int field, ConcurrentHashMap<NDD, Long> edges) {
        if (edges.isEmpty()) {
            // Since NDD omits all edges pointing to FALSE, the empty edge represents FALSE.
            return NDD.getFalse();
        } else if (edges.size() == 1 && edges.values().iterator().next() == 1) {
            // Omit nodes with the only edge labeled by BDD TRUE.
            return edges.keySet().iterator().next();
        } else {
            NDD node = nodeTable.get(field).get(edges);
            if (node == null) {
                // create a new node
                // 1. add ref count of all descendants
                for (NDD descendant : edges.keySet()) {
                    if (!descendant.isTerminal()) {
                        referenceCount.put(descendant, referenceCount.get(descendant) + 1);
                    }
                }

                // 2. check if there should be a gc or grow
                if (currentSize >= nddTableSize) {
                    gcOrGrow();
                }

                // 3. create node
                NDD newNode = new NDD(field, edges);
                nodeTable.get(field).put(edges, newNode);
                referenceCount.put(newNode, 0);
                currentSize++;
                return newNode;
            } else {
                // reuse node
                for (Long bdd : edges.values()) {
                    JSylvan.deref(bdd);
                }
                return node;
            }
        }
    }

    /**
     * Free unused ndd node, first by garbage collection, then by growing the node
     * table.
     */
    private void gcOrGrow() {
        gc();
        if (nddTableSize - currentSize <= nddTableSize * QUICK_GROW_THRESHOLD) {
            grow();
        }
        NDD.clearCaches();
    }

    /**
     * Garbage collection.
     */
    private void gc() {
        // protect temporary nodes during NDD operations
        for (NDD ndd : NDD.getTemporarilyProtect()) {
            ref(ndd);
        }

        // remove unused nodes by topological sorting
        Queue<NDD> deadNodesQueue = new LinkedList<>();
        for (Map.Entry<NDD, Integer> entry : referenceCount.entrySet()) {
            if (entry.getValue() == 0) {
                deadNodesQueue.offer(entry.getKey());
            }
        }
        while (!deadNodesQueue.isEmpty()) {
            NDD deadNode = deadNodesQueue.poll();
            for (NDD descendant : deadNode.getEdges().keySet()) {
                if (descendant.isTerminal())
                    continue;
                int newReferenceCount = referenceCount.get(descendant) - 1;
                referenceCount.put(descendant, newReferenceCount);
                if (newReferenceCount == 0) {
                    deadNodesQueue.offer(descendant);
                }
            }
            // delete current dead node
            for (long bddLabel : deadNode.getEdges().values()) {
                JSylvan.deref(bddLabel);
            }
            referenceCount.remove(deadNode);
            nodeTable.get(deadNode.getField()).remove(deadNode.getEdges());
            currentSize--;
        }

        for (NDD ndd : NDD.getTemporarilyProtect()) {
            deref(ndd);
        }
    }

    /**
     * Grow the node table.
     */
    private void grow() {
        nddTableSize *= 2;
    }

    /**
     * Protect a root node from garbage collection.
     * 
     * @param ndd The root to be protected.
     * @return The ndd node.
     */
    public NDD ref(NDD ndd) {
        if (!ndd.isTerminal() && referenceCount.get(ndd) != Integer.MAX_VALUE) {
            referenceCount.put(ndd, referenceCount.get(ndd) + 1);
        }
        return ndd;
    }

    /**
     * Ref the initialized NDD node with Integer.MAX_VALUE (special label)
     * 
     * @param ndd
     */
    public void fixNDDNodeRefCount(NDD ndd) {
        referenceCount.put(ndd, Integer.MAX_VALUE);
    }

    /**
     * Unprotect a root node, such that the node can be cleared during garbage
     * collection.
     * 
     * @param ndd The ndd node to be unprotected.
     */
    public void deref(NDD ndd) {
        if (!ndd.isTerminal() && referenceCount.get(ndd) != Integer.MAX_VALUE) {
            referenceCount.put(ndd, referenceCount.get(ndd) - 1);
        }
    }
}
