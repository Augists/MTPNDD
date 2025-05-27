/**
 * Implement logical operations of NDD.
 * @author Zechun Li - XJTU ANTS NetVerify Lab
 * @version 1.0
 */
package org.ants.jpndd.diagram;

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

import org.ants.jpndd.cache.OperationCache;
import org.ants.jpndd.nodetable.NodeTable;
import org.ants.jpndd.utils.DecomposeBDD;

import javafx.util.Pair;
import jsylvan.JSylvan;

public class NDD {
    /**
     * The size of each operation cache.
     */
    private static int CACHE_SIZE = 10000;
    private final static boolean DEBUG_MODEL = false;

    /**
     * The ndd node table.
     */
    private static NodeTable nodeTable;

    /**
     * The number of fields.
     */
    protected static int fieldNum;

    /**
     * The max id of bits for each field.
     */
    private static ArrayList<Integer> maxVariablePerField;

    /**
     * The correction factor of each field, used by operation of satCount.
     */
    private static ArrayList<Double> satCountDiv;

    /**
     * All bdd variables.
     */
    private static ArrayList<long[]> bddVarsPerField;

    /**
     * The negation of each ndd variable.
     */
    private static ArrayList<long[]> bddNotVarsPerField;

    /**
     * All ndd variables.
     */
    private static ArrayList<NDD[]> nddVarsPerField;

    /**
     * The negation of each ndd variable.
     */
    private static ArrayList<NDD[]> nddNotVarsPerField;

    /**
     * Temporary ndd nodes during a logical operation, which should be protected
     * during garbage collection.
     */
    private static HashSet<NDD> temporarilyProtect;

    /**
     * The cache of operation NOT.
     */
    private static OperationCache<NDD> notCache;
    /**
     * The cache of operation AND.
     */
    private static OperationCache<NDD> andCache;
    /**
     * The cache of operation OR.
     */
    private static OperationCache<NDD> orCache;

    /**
     * Init the NDD engine.
     * 
     * @param nddTableSize The max size of ndd node table.
     * @param bddTableSize The max size of bdd node table.
     * @param bddCacheSize The max size of bdd operation cache.
     */
    public static void initNDD(int nddTableSize, int bddTableSize, int bddCacheSize, long sylvanMaxMemory) {
        nodeTable = new NodeTable(nddTableSize, bddTableSize, bddCacheSize, sylvanMaxMemory);
        fieldNum = -1;
        maxVariablePerField = new ArrayList<>();
        satCountDiv = new ArrayList<>();
        bddVarsPerField = new ArrayList<>();
        bddNotVarsPerField = new ArrayList<>();
        nddVarsPerField = new ArrayList<>();
        nddNotVarsPerField = new ArrayList<>();
        temporarilyProtect = new HashSet<>();
        notCache = new OperationCache<>(CACHE_SIZE, 2);
        andCache = new OperationCache<>(CACHE_SIZE, 3);
        orCache = new OperationCache<>(CACHE_SIZE, 3);
    }

    /**
     * Initialize the NDD engine with user-defined cache size.
     * @param nddTableSize The max size of ndd node table.
     * @param nddCacheSize The size of ndd cache (default 10000).
     * @param bddTableSize The max size of bdd node table.
     * @param bddCacheSize The max size of bdd operation cache.
     */
    public static void initNDD(int nddTableSize, int nddCacheSize, int bddTableSize, int bddCacheSize, long sylvanMaxMemory) {
        CACHE_SIZE = nddCacheSize;
        initNDD(nddTableSize, bddTableSize, bddCacheSize, sylvanMaxMemory);
    }

    /**
     * declare a new field of 'bitNum' bits.
     * 
     * @param bitNum The number of bits in the field.
     * @return The id of the field.
     */
    public static int declareField(int bitNum) {
        // 1. update the number of fields
        fieldNum++;
        // 2. update the boundary of each field
        if (maxVariablePerField.isEmpty()) {
            maxVariablePerField.add(bitNum - 1);
        } else {
            maxVariablePerField.add(maxVariablePerField.get(maxVariablePerField.size() - 1) + bitNum);
        }
        // 3. update satCountDiv, which will be used in satCount operation of NDD
        double factor = Math.pow(2.0, bitNum);
        for (int i = 0; i < satCountDiv.size(); i++) {
            satCountDiv.set(i, satCountDiv.get(i) * factor);
        }
        int totalBitsBefore = 0;
        if (maxVariablePerField.size() > 1) {
            totalBitsBefore = maxVariablePerField.get(maxVariablePerField.size() - 2) + 1;
        }
        satCountDiv.add(Math.pow(2.0, totalBitsBefore));
        // 4. add node table
        nodeTable.declareField();
        // 5. declare vars
        long[] bddVars = new long[bitNum];
        long[] bddNotVars = new long[bitNum];
        NDD[] nddVars = new NDD[bitNum];
        NDD[] nddNotVars = new NDD[bitNum];

        for (int i = 0; i < bitNum; i++) {
            bddVars[i] = JSylvan.ref(JSylvan.makeVar(totalBitsBefore + i + 1));
            bddNotVars[i] = JSylvan.ref(JSylvan.makeNot(bddVars[i]));

            Map<NDD, Long> edges = new HashMap<>();
            edges.put(getTrue(), JSylvan.ref(bddVars[i]));
            nddVars[i] = mk(fieldNum, edges);
            nodeTable.fixNDDNodeRefCount(nddVars[i]);

            edges = new HashMap<>();
            edges.put(getTrue(), JSylvan.ref(bddNotVars[i]));
            nddNotVars[i] = mk(fieldNum, edges);
            nodeTable.fixNDDNodeRefCount(nddNotVars[i]);
        }
        bddVarsPerField.add(bddVars);
        bddNotVarsPerField.add(bddNotVars);
        nddVarsPerField.add(nddVars);
        nddNotVarsPerField.add(nddNotVars);
        return fieldNum;
    }

    public static int getFieldNum() {
        return fieldNum;
    }

    /**
     * Get the ndd variable of a specific bit.
     * 
     * @param field The id of the field.
     * @param index The id of the bit in the field.
     * @return The ndd variable.
     */
    public static NDD getVar(int field, int index) {
        return nddVarsPerField.get(field)[index];
    }
    
    /**
     * Get the negation the variable for a specific bit.
     * 
     * @param field The id of the field.
     * @param index The id of the bit in the field.
     * @return The negation of the ndd variable.
     */
    public static NDD getNotVar(int field, int index) {
        return nddNotVarsPerField.get(field)[index];
    }

    public static long[] getBDDVars(int field) {
        return bddVarsPerField.get(field);
    }

    public static long[] getNotBDDVars(int field) {
        return bddNotVarsPerField.get(field);
    }

    /**
     * Clear all the caches, the api is usually invoked during garbage collection.
     */
    public static void clearCaches() {
        notCache.clearCache();
        andCache.clearCache();
        orCache.clearCache();
    }

    /**
     * Protect a root node from garbage collection.
     * 
     * @param ndd The root to be protected.
     * @return The ndd node.
     */
    public static NDD ref(NDD ndd) {
        return nodeTable.ref(ndd);
    }

    /**
     * Unprotect a root node, such that the node can be cleared during garbage
     * collection.
     * 
     * @param ndd The ndd node to be unprotected.
     */
    public static void deref(NDD ndd) {
        nodeTable.deref(ndd);
    }

    /**
     * Get all the temporary nodes.
     * 
     * @return All the temporary nodes.
     */
    public static HashSet<NDD> getTemporarilyProtect() {
        return temporarilyProtect;
    }

    /**
     * The logical operation AND, which automatically ref the result and deref the
     * first operand.
     * 
     * @param a The first operand.
     * @param b The second operand.
     * @return The result of the logical operation.
     */
    public static NDD andTo(NDD a, NDD b) {
        NDD result = ref(and(a, b));
        deref(a);
        if (DEBUG_MODEL) {
            long aBDD = JSylvan.ref(toBDD(a));
            long bBDD = JSylvan.ref(toBDD(b));
            long resultBDD = JSylvan.ref(JSylvan.makeAnd(aBDD, bBDD));
            JSylvan.deref(aBDD);
            JSylvan.deref(bBDD);
            if (resultBDD != toBDD(result)) {
                System.out.println("Operation and: result wrong!");
            }
            JSylvan.deref(resultBDD);
        }
        return result;
    }

    /**
     * The logical operation OR, which automatically ref the result and deref the
     * first operand.
     * 
     * @param a The first operand.
     * @param b The second operand.
     * @return The result of the logical operation.
     */
    public static NDD orTo(NDD a, NDD b) {
        NDD result = ref(or(a, b));
        deref(a);
        if (DEBUG_MODEL) {
            long aBDD = JSylvan.ref(toBDD(a));
            long bBDD = JSylvan.ref(toBDD(b));
            long resultBDD = JSylvan.ref(JSylvan.makeOr(aBDD, bBDD));
            JSylvan.deref(aBDD);
            JSylvan.deref(bBDD);
            if (resultBDD != toBDD(result)) {
                System.out.println("Operation or: result wrong!");
            }
            JSylvan.deref(resultBDD);
        }
        return result;
    }

    /**
     * Add an edge into a set of edges, may merge some edges.
     * 
     * @param edges      A set of edges.
     * @param descendant The descendant of the edge to be inserted.
     * @param labelBDD   The label of the edge to be inserted.
     */
    protected static void addEdge(Map<NDD, Long> edges, NDD descendant, long labelBDD) {
        // omit the edge pointing to terminal node FALSE
        if (descendant.isFalse()) {
            JSylvan.deref(labelBDD);
            return;
        }
        // try to find the edge pointing to the same descendant
        Long oldLabel = JSylvan.getFalse();
        if (edges.containsKey(descendant)) {
            oldLabel = edges.get(descendant);
        }
        // merge the bdd label
        long newLabel = JSylvan.ref(JSylvan.makeOr(oldLabel, labelBDD));
        JSylvan.deref(labelBDD);
        JSylvan.deref(oldLabel);
        edges.put(descendant, newLabel);
    }

    /**
     * The logical operation AND.
     * 
     * @param a The first operand.
     * @param b The second operand.
     * @return The result of the logical operation.
     */
    public static NDD and(NDD a, NDD b) {
        temporarilyProtect.clear();
        NDD result = andRec(a, b);
        if (DEBUG_MODEL) {
            long aBDD = JSylvan.ref(toBDD(a));
            long bBDD = JSylvan.ref(toBDD(b));
            long resultBDD = JSylvan.ref(JSylvan.makeAnd(aBDD, bBDD));
            JSylvan.deref(aBDD);
            JSylvan.deref(bBDD);
            if (resultBDD != toBDD(result)) {
                System.out.println("Operation and: result wrong!");
            }
            JSylvan.deref(resultBDD);
        }
        return result;
    }

    // static ReentrantLock bdd_lock = new ReentrantLock();

    /**
     * The recursive implementation of the logical operation AND.
     * 
     * @param a The first operand.
     * @param b The second operand.
     * @return The result of the logical operation.
     */
    private static NDD andRec(NDD a, NDD b) {
        // terminal condition
        if (a.isFalse() || b.isTrue()) {
            return a;
        } else if (a.isTrue() || b.isFalse() || a == b) {
            return b;
        }

        // check the cache
        if (andCache.getEntry(a, b))
            return andCache.result;

        Map<NDD, Long> edges = new HashMap<>();
        if (a.field == b.field) {
            // // compute the intersection of the edges in parallel
            // a.edges.entrySet().parallelStream().forEach(entryA -> {
            //     // System.out.println("outer " + Thread.currentThread().getName() + " processing" + " from " + entryA.getKey());

            //     b.edges.entrySet().parallelStream().forEach(entryB -> {
            //         // System.out.println("inner " + Thread.currentThread().getName() + " processing" + " from " + entryA.getKey() + " for " + entryB.getKey());

            //         // System.out.println("trying to get lock " + Thread.currentThread().getName() + " from " + entryA.getKey() + " for " + entryB.getKey());
            //         bdd_lock.lock();
            //         // System.out.println("successfully get lock " + Thread.currentThread().getName() + " from " + entryA.getKey() + " for " + entryB.getKey());
            //         try {
            //             long intersect = JSylvan.ref(JSylvan.makeAnd(entryA.getValue(), entryB.getValue()));
            //             if (intersect != 0) {
            //                 // the descendant of the new edge
            //                 // System.out.println("digui" + " from " + entryA.getKey() + " for " + entryB.getKey());
            //                 NDD subResult = andRec(entryA.getKey(), entryB.getKey());
            //                 // try to merge edges
            //                 addEdge(edges, subResult, intersect);
            //             }
            //         } finally {
            //             // System.out.println("trying to free lock " + Thread.currentThread().getName() + " from " + entryA.getKey() + " for " + entryB.getKey());
            //             bdd_lock.unlock();
            //             // System.out.println("successfully free lock " + Thread.currentThread().getName() + " from " + entryA.getKey() + " for " + entryB.getKey());
            //         }
            //     });
            // });

            for (Map.Entry<NDD, Long> entryA : a.edges.entrySet()) {
                for (Map.Entry<NDD, Long> entryB : b.edges.entrySet()) {
                    // the bdd label on the new edge
                    long intersect = JSylvan.ref(JSylvan.makeAnd(entryA.getValue(), entryB.getValue()));
                    if (intersect != JSylvan.getFalse()) {
                        // the descendant of the new edge
                        NDD subResult = andRec(entryA.getKey(), entryB.getKey());
                        // try to merge edges
                        addEdge(edges, subResult, intersect);
                    }
                }
            }
        } else {
            if (a.field > b.field) {
                NDD t = a;
                a = b;
                b = t;
            }
            // a.edges.entrySet().parallelStream().forEach(entryA -> {
            //     /*
            //      * if A branches on a higher field than B,
            //      * we can let A operate with a pseudo node
            //      * with only edge labelled by true and pointing to B
            //      */
            //     NDD subResult = andRec(entryA.getKey(), b);
            //     addEdge(edges, subResult, JSylvan.ref(entryA.getValue()));
            // });
            for (Map.Entry<NDD, Long> entryA : a.edges.entrySet()) {
                /*
                 * if A branches on a higher field than B,
                 * we can let A operate with a pseudo node
                 * with only edge labelled by true and pointing to B
                 */
                NDD subResult = andRec(entryA.getKey(), b);
                addEdge(edges, subResult, JSylvan.ref(entryA.getValue()));
            }
        }
        // try to create or reuse node
        NDD result = mk(a.field, edges);
        // protect the node during the operation
        temporarilyProtect.add(result);
        // store the result into cache
        andCache.setEntry(andCache.hashValue, a, b, result);
        return result;
    }

    /**
     * The logical operation OR.
     * 
     * @param a The first operand.
     * @param b The second operand.
     * @return The result of the logical operation.
     */
    public static NDD or(NDD a, NDD b) {
        temporarilyProtect.clear();
        NDD result = orRec(a, b);
        if (DEBUG_MODEL) {
            long aBDD = JSylvan.ref(toBDD(a));
            long bBDD = JSylvan.ref(toBDD(b));
            long resultBDD = JSylvan.ref(JSylvan.makeOr(aBDD, bBDD));
            JSylvan.deref(aBDD);
            JSylvan.deref(bBDD);
            if (resultBDD != toBDD(result)) {
                System.out.println("Operation or: result wrong!");
            }
            JSylvan.deref(resultBDD);
        }
        return result;
    }

    /**
     * The recursive implementation of the logical operation OR.
     * 
     * @param a The first operand.
     * @param b The second operand.
     * @return The result of the logical operation.
     */
    private static NDD orRec(NDD a, NDD b) {
        // terminal condition
        if (a.isTrue() || b.isFalse()) {
            return a;
        } else if (a.isFalse() || b.isTrue() || a == b) {
            return b;
        }

        // check the cache
        if (orCache.getEntry(a, b))
            return orCache.result;

        Map<NDD, Long> edges = new HashMap<>();
        if (a.field == b.field) {
            // record edges of each node, which will 'or' with the edge pointing to FALSE of
            // another node
            Map<NDD, Long> residualA = new HashMap<>(a.edges);
            Map<NDD, Long> residualB = new HashMap<>(b.edges);
            for (long oneBDD : a.edges.values()) {
                JSylvan.ref(oneBDD);
            }
            for (long oneBDD : b.edges.values()) {
                JSylvan.ref(oneBDD);
            }
            // a.edges.entrySet().parallelStream().forEach(entryA -> {
            //     b.edges.entrySet().parallelStream().forEach(entryB -> {
            //         // the bdd label on the new edge
            //         long intersect = JSylvan.ref(JSylvan.makeAnd(entryA.getValue(), entryB.getValue()));
            //         if (intersect != 0) {
            //             // update residual
            //             long notIntersect = JSylvan.ref(JSylvan.makeNot(intersect));
            //             long oldResidual = residualA.get(entryA.getKey());
            //             residualA.put(entryA.getKey(), JSylvan.makeAnd(oldResidual, notIntersect));
            //             oldResidual = residualB.get(entryB.getKey());
            //             residualB.put(entryB.getKey(), JSylvan.makeAnd(oldResidual, notIntersect));
            //             JSylvan.deref(notIntersect);
            //             // the descendant of the new edge
            //             NDD subResult = orRec(entryA.getKey(), entryB.getKey());
            //             // try to merge edges
            //             addEdge(edges, subResult, intersect);
            //         }
            //     });
            // });

            for (Map.Entry<NDD, Long> entryA : a.edges.entrySet()) {
                for (Map.Entry<NDD, Long> entryB : b.edges.entrySet()) {
                    // the bdd label on the new edge
                    long intersect = JSylvan.ref(JSylvan.makeAnd(entryA.getValue(), entryB.getValue()));
                    if (intersect != JSylvan.getFalse()) {
                        // update residual
                        long notIntersect = JSylvan.ref(JSylvan.makeNot(intersect));
                        long oldResidual = residualA.get(entryA.getKey());
                        residualA.put(entryA.getKey(), JSylvan.ref(JSylvan.makeAnd(oldResidual, notIntersect)));
                        JSylvan.deref(oldResidual);
                        oldResidual = residualB.get(entryB.getKey());
                        residualB.put(entryB.getKey(), JSylvan.ref(JSylvan.makeAnd(oldResidual, notIntersect)));
                        JSylvan.deref(oldResidual);
                        JSylvan.deref(notIntersect);
                        // the descendant of the new edge
                        NDD subResult = orRec(entryA.getKey(), entryB.getKey());
                        // try to merge edges
                        addEdge(edges, subResult, intersect);
                    }
                }
            }
            /*
             * Each residual of A doesn't match with any explicit edge of B,
             * and will match with the edge pointing to FALSE of B, which is omitted.
             * The situation is the same for B.
             */
            // residualA.entrySet().parallelStream().forEach(entryA -> {
            //     if (entryA.getValue() != 0) {
            //         addEdge(edges, entryA.getKey(), JSylvan.ref(entryA.getValue()));
            //     }
            // });
            for (Map.Entry<NDD, Long> entryA : residualA.entrySet()) {
                if (entryA.getValue() != JSylvan.getFalse()) {
                    addEdge(edges, entryA.getKey(), JSylvan.ref(entryA.getValue()));
                }
            }
            // residualB.entrySet().parallelStream().forEach(entryB -> {
            //     if (entryB.getValue() != 0) {
            //         addEdge(edges, entryB.getKey(), JSylvan.ref(entryB.getValue()));
            //     }
            // });
            for (Map.Entry<NDD, Long> entryB : residualB.entrySet()) {
                if (entryB.getValue() != JSylvan.getFalse()) {
                    addEdge(edges, entryB.getKey(), JSylvan.ref(entryB.getValue()));
                }
            }
        } else {
            if (a.field > b.field) {
                NDD t = a;
                a = b;
                b = t;
            }
            long residualB = JSylvan.getTrue();
            // a.edges.entrySet().parallelStream().forEach(entryA -> {
            //     long notIntersect = JSylvan.ref(JSylvan.makeNot(entryA.getValue()));
            //     residualB = JSylvan.makeAnd(residualB, notIntersect);
            //     JSylvan.deref(notIntersect);
            //     NDD subResult = orRec(entryA.getKey(), b);
            //     addEdge(edges, subResult, JSylvan.ref(entryA.getValue()));
            // });

            for (Map.Entry<NDD, Long> entryA : a.edges.entrySet()) {
                /*
                 * if A branches on a higher field than B,
                 * we can let A operate with a pseudo node
                 * with only edge labelled by true and pointing to B
                 */
                long notIntersect = JSylvan.ref(JSylvan.makeNot(entryA.getValue()));
                long temp = residualB;
                residualB = JSylvan.ref(JSylvan.makeAnd(residualB, notIntersect));
                JSylvan.deref(temp);
                JSylvan.deref(notIntersect);
                NDD subResult = orRec(entryA.getKey(), b);
                addEdge(edges, subResult, JSylvan.ref(entryA.getValue()));
            }
            if (residualB != JSylvan.getFalse()) {
                addEdge(edges, b, residualB);
            }
        }
        // try to create or reuse node
        NDD result = mk(a.field, edges);
        // protect the node during the operation
        temporarilyProtect.add(result);
        // store the result into cache
        orCache.setEntry(orCache.hashValue, a, b, result);
        return result;
    }

    /**
     * The logical operation NOT.
     * 
     * @param a The operand.
     * @return The result of the logical operation.
     */
    public static NDD not(NDD a) {
        temporarilyProtect.clear();
        NDD result = notRec(a);
        if (DEBUG_MODEL) {
            long aBDD = JSylvan.ref(toBDD(a));
            long resultBDD = JSylvan.ref(JSylvan.makeNot(aBDD));
            JSylvan.deref(aBDD);
            if (resultBDD != toBDD(result)) {
                System.out.println("Operation not: result wrong!");
            }
            JSylvan.deref(resultBDD);
        }
        return result;
    }

    /**
     * The recursive implementation of the logical operation NOT.
     * 
     * @param a The operand.
     * @return The result of the logical operation.
     */
    private static NDD notRec(NDD a) {
        if (a.isTrue()) {
            return FALSE;
        } else if (a.isFalse()) {
            return TRUE;
        }

        if (notCache.getEntry(a))
            return notCache.result;

        Map<NDD, Long> edges = new HashMap<>();
        long residual = JSylvan.getTrue();
        for (Map.Entry<NDD, Long> entryA : a.edges.entrySet()) {
            long notIntersect = JSylvan.ref(JSylvan.makeNot(entryA.getValue()));
            long temp = residual;
            residual = JSylvan.ref(JSylvan.makeAnd(residual, notIntersect));
            JSylvan.deref(temp);
            JSylvan.deref(notIntersect);
            NDD subResult = notRec(entryA.getKey());
            addEdge(edges, subResult, JSylvan.ref(entryA.getValue()));
        }
        if (residual != JSylvan.getFalse()) {
            addEdge(edges, TRUE, residual);
        }
        NDD result = mk(a.field, edges);
        temporarilyProtect.add(result);
        notCache.setEntry(notCache.hashValue, a, result);
        return result;
    }

    // a / b <==> a ∩ (not b)
    /**
     * The logical operation DIFF, which is equivalent to a AND (NOT b).
     * 
     * @param a The operand.
     * @return The result of the logical operation.
     */
    public static NDD diff(NDD a, NDD b) {
        temporarilyProtect.clear();
        NDD n = notRec(b);
        temporarilyProtect.add(n);
        NDD result = andRec(a, n);
        if (DEBUG_MODEL) {
            long aBDD = JSylvan.ref(toBDD(a));
            long bBDD = JSylvan.ref(toBDD(b));
            long t = JSylvan.ref(JSylvan.makeNot(bBDD));
            JSylvan.deref(bBDD);
            long resultBDD = JSylvan.ref(JSylvan.makeAnd(aBDD, t));
            JSylvan.deref(aBDD);
            JSylvan.deref(t);
            if (resultBDD != toBDD(result)) {
                System.out.println("Operation diff: result wrong!");
            }
            JSylvan.deref(resultBDD);
        }
        return result;
    }

    /**
     * The existential quantification.
     * 
     * @param a     The operand.
     * @param field The field to run an existential quantification.
     * @return The result.
     */
    public static NDD exist(NDD a, int field) {
        temporarilyProtect.clear();
        return existRec(a, field);
    }

    /**
     * The recursive implementation of existential quantification.
     * 
     * @param a     The operand.
     * @param field The field to run an existential quantification.
     * @return The result.
     */
    private static NDD existRec(NDD a, int field) {
        if (a.isTerminal() || a.field > field) {
            return a;
        }

        NDD result = FALSE;
        if (a.field == field) {
            // a.edges.entrySet().parallelStream().forEach(entryA -> {
            //     result[0] = orRec(result[0], entryA.getKey());
            // });
            for (NDD next : a.edges.keySet()) {
                result = orRec(result, next);
            }
        } else {
            Map<NDD, Long> edges = new HashMap<>();
            // a.edges.entrySet().parallelStream().forEach(entryA -> {
            //     NDD subResult = existRec(entryA.getKey(), field);
            //     addEdge(edges, subResult, JSylvan.ref(entryA.getValue()));
            // });
            for (Map.Entry<NDD, Long> entryA : a.edges.entrySet()) {
                NDD subResult = existRec(entryA.getKey(), field);
                addEdge(edges, subResult, JSylvan.ref(entryA.getValue()));
            }
            result = mk(a.field, edges);
        }
        temporarilyProtect.add(result);
        return result;
    }

    // a => b <==> (not a) ∪ b
    /**
     * The logical implication, which is equivalent to (NOT a) OR b.
     * 
     * @param a The first operand.
     * @param b The second operand.
     * @return The result of the logical implication.
     */
    public static NDD imp(NDD a, NDD b) {
        temporarilyProtect.clear();
        NDD n = notRec(a);
        temporarilyProtect.add(n);
        NDD result = orRec(n, b);
        return result;
    }

    /**
     * The number of solutions encoded in the ndd node.
     * 
     * @param ndd The ndd node.
     * @return The number of solutions.
     */
    public static double satCount(NDD ndd) {
        // double result = satCountRec(ndd, 0);
        // if (DEBUG_MODEL) {
        // double bddResult = bddEngine.satCount(toBDD(ndd));
        // if (result != bddResult) {
        // System.out.println("Operation satCount: result wrong!");
        // }
        // }
        // return result;

        int[] variableset = new int[maxVariablePerField.get(fieldNum) + 1];
        for (int i = 0; i < maxVariablePerField.get(fieldNum) + 1; i++)
            variableset[i] = i + 1;
        long SetofVariable = JSylvan.ref(JSylvan.makeSet(variableset));

        // long bddQueen = toBDD(ndd);
        // System.out.println("bdd queen");
        // System.out.println("length " + (maxVariablePerField.get(fieldNum) + 1));
        // System.out.println("set of variable " + SetofVariable);
        // JSylvan.printDot(bddQueen);

        return JSylvan.satcount(toBDD(ndd), SetofVariable);
    }

    /**
     * The recursive implementation of satCount.
     * 
     * @param curr  Current ndd node.
     * @param field Current field.
     * @return The number of solutions.
     */
    /*
    private static double satCountRec(NDD curr, int field) {
        if (curr.isFalse()) {
            return 0;
        } else if (curr.isTrue()) {
            if (field > fieldNum) {
                return 1;
            } else {
                int len = maxVariablePerField.get(maxVariablePerField.size() - 1);
                if (field == 0) {
                    len++;
                } else {
                    len -= maxVariablePerField.get(field - 1);
                }
                return Math.pow(2.0, len);
            }
        } else {
            double result = 0;
            if (field == curr.field) {
                for (Map.Entry<NDD, Long> entry : curr.edges.entrySet()) {
                    double bddSat = JSylvan.satCount(entry.getValue()) / satCountDiv.get(curr.field);
                    // System.out.println(bddEngine.satCount(entry.getValue()) + " " + bddSat);
                    double nddSat = satCountRec(entry.getKey(), field + 1);
                    result += bddSat * nddSat;
                }
            } else {
                int len = maxVariablePerField.get(field);
                if (field == 0) {
                    len++;
                } else {
                    len -= maxVariablePerField.get(field - 1);
                }
                result = Math.pow(2.0, len) * satCountRec(curr, field + 1);
            }
            return result;
        }
    }
    */

    /**
     * Encode an NDD of a prefix with no temporary NDD nodes created.
     * 
     * @param prefixBinary The binary prefix, e.g., [1, 0, 1, 0] for 10.
     * @param field        The field of the prefix.
     * @return An ndd node encoding the prefix.
     */
    public static NDD encodePrefix(int[] prefixBinary, int field) {
        if (prefixBinary.length == 0) {
            return TRUE;
        }

        long prefixBDD = encodePrefixBDD(prefixBinary, getBDDVars(field), getNotBDDVars(field));

        Map<NDD, Long> edges = new HashMap<>();
        edges.put(TRUE, prefixBDD);
        return mk(field, edges);
    }

    public static NDD encodePrefixs(ArrayList<int[]> prefixsBinary, int field) {
        long prefixsBDD = JSylvan.getFalse();
        for (int[] prefix : prefixsBinary) {
            long temp = prefixsBDD;
            prefixsBDD = JSylvan.makeOr(prefixsBDD, encodePrefixBDD(prefix, getBDDVars(field), getNotBDDVars(field)));
            JSylvan.deref(temp);
        }
        Map<NDD, Long> edges = new HashMap<>();
        edges.put(TRUE, prefixsBDD);
        return mk(field, edges);
    }

    private static long encodePrefixBDD(int[] prefixBinary, long[] vars, long[] notVars) {
        if (prefixBinary.length == 0) {
            return JSylvan.getTrue();
        }

        long prefixBDD = JSylvan.getTrue();
        for (int i = prefixBinary.length - 1; i >= 0; i--) {
            long currentBit = prefixBinary[i] == 1 ? vars[i] : notVars[i];
            if (i == prefixBinary.length - 1) {
                prefixBDD = JSylvan.ref(currentBit);
            } else {
                long temp = prefixBDD;
                prefixBDD = JSylvan.ref(JSylvan.makeAnd(prefixBDD, currentBit));
                JSylvan.deref(temp);
            }
        }
        return prefixBDD;
    }

    /**
     * <field, bdd>, entries in perFieldBDD must follow the order with field asc
     */
    public static NDD encodeACL(ArrayList<Pair<Integer, Long>> perFieldBDD) {
        NDD result = TRUE;
        for (int i = perFieldBDD.size() - 1; i >= 0; i--) {
            if (perFieldBDD.get(i).getValue() != JSylvan.getTrue()) {
                Map<NDD, Long> edges = new HashMap<>();
                edges.put(result, perFieldBDD.get(i).getValue());
                result = mk(perFieldBDD.get(i).getKey(), edges);
            }
        }
        return result;
    }

    public static NDD toNDD(long a, int field) {
        return toNDDFunc(a, field);
    }

    private static NDD toNDDFunc(long a, int field) {
        if (a == JSylvan.getTrue()) {
            return TRUE;
        } else {
            Map<NDD, Long> edges = new HashMap<>();
            edges.put(TRUE, a);
            return mk(field, edges);
        }
    }

    public static NDD toNDD(long a) {
        return toNDDFunc(a);
    }

    private static NDD toNDDFunc(long a) {
        Map<Long, HashMap<Long, Long>> decomposed = DecomposeBDD.decompose(a, maxVariablePerField);
        Map<Long, NDD> converted = new HashMap<>();
        converted.put(JSylvan.getTrue(), TRUE);
        while (!decomposed.isEmpty()) {
            Set<Long> finished = converted.keySet();
            for (Map.Entry<Long, HashMap<Long, Long>> entry : decomposed.entrySet()) {
                if (finished.containsAll(entry.getValue().keySet())) {
                    int field = DecomposeBDD.bddGetField(entry.getKey());
                    Map<NDD, Long> map = new HashMap<>();
                    for (Map.Entry<Long, Long> entry1 : entry.getValue().entrySet()) {
                        map.put(converted.get(entry1.getKey()), JSylvan.ref(entry1.getValue()));
                    }
                    NDD n = mk(field, map);
                    converted.put(entry.getKey(), n);
                    decomposed.remove(entry.getKey());
                    break;
                }
            }
        }
        for (Map<Long, Long> map : decomposed.values()) {
            for (Long pred : map.values()) {
                JSylvan.deref(pred);
            }
        }
        return converted.get(a);
    }

    public static ArrayList<long[]> toArray(NDD curr) {
        ArrayList<long[]> array = new ArrayList<>();
        long[] vec = new long[fieldNum + 1];
        toArrayRec(curr, array, vec, 0);
        return array;
    }

    private static void toArrayRec(NDD curr, ArrayList<long[]> array, long[] vec, int currField) {
        if (curr.isFalse()) {
        } else if (curr.isTrue()) {
            for (int i = currField; i <= fieldNum; i++) {
                vec[i] = JSylvan.getTrue();
            }
            long[] temp = new long[fieldNum + 1];
            for (int i = 0; i <= fieldNum; i++) {
                temp[i] = vec[i];
            }
            array.add(temp);
        } else {
            for (int i = currField; i < curr.field; i++) {
                vec[i] = JSylvan.getTrue();
            }
            for (Map.Entry<NDD, Long> entry : curr.edges.entrySet()) {
                vec[curr.field] = entry.getValue();
                toArrayRec(entry.getKey(), array, vec, curr.field + 1);
            }
        }
    }

    public static long toBDD(NDD root) {
        long result = toBDDRec(root);
        JSylvan.deref(result);
        return result;
    }

    /**
     * The recursive implementation of toBDD.
     * 
     * @param current The current ndd node.
     * @return The bdd node.
     */
    private static long toBDDRec(NDD current) {
        if (current.isTrue()) {
            return JSylvan.getTrue();
        } else if (current.isFalse()) {
            return JSylvan.getFalse();
        } else {
            long result = JSylvan.getFalse();
            for (Map.Entry<NDD, Long> entry : current.edges.entrySet()) {
                long child = toBDDRec(entry.getKey());
                long children = JSylvan.ref(JSylvan.makeAnd(child, entry.getValue()));
                JSylvan.deref(child);
                long temp = result;
                result = JSylvan.ref(JSylvan.makeOr(result, children));
                JSylvan.deref(temp);
                JSylvan.deref(children);
            }
            return result;
        }
    }

    public static void print(NDD root) {
        System.out.println("Print " + root + " begin!");
        printRec(root);
        System.out.println("Print " + root + " finish!\n");
    }

    private static void printRec(NDD current) {
        if (current.isTrue())
            System.out.println("TRUE");
        else if (current.isFalse())
            System.out.println("FALSE");
        else {
            System.out.println("field:" + current.field + " node:" + current);
            for (Map.Entry<NDD, Long> entry : current.getEdges().entrySet()) {
                System.out.println("next:" + entry.getKey() + " label:" + entry.getValue());
            }
            for (NDD next : current.getEdges().keySet()) {
                printRec(next);
            }
        }
    }

    private static PrintStream ps = null;
    private static Map<NDD, Boolean> visitedNDD = new HashMap<>();
    private static Map<Long, Boolean> visitedBDD = new HashMap<>();

    public static void printDot(String filename, NDD root) {
        try {
            ps = new PrintStream(new FileOutputStream(filename));
            ps.println("digraph NDD {");
            ps.println("\tinit__ [label=\"init\", style=invis, height=0, width=0];");
            ps.println("\tinit__ -> " + root + ";");

            printDot_rec(root);

            ps.println("\t" + getFalse() + " [shape=box, label=\"FALSE\", style=filled, shape=box, height=0.3, width=0.3];");
            ps.println("\t" + getTrue() + " [shape=box, label=\"TRUE\", style=filled, shape=box, height=0.3, width=0.3];");

            ps.println("}");
            ps.close();
        } catch (IOException e) {
        }
    }

    private static void printDot_rec(NDD current) {
        if (visitedNDD.containsKey(current)) {
            return;
        }
        visitedNDD.put(current, true);
        if (current.isTerminal()) {
            return;
        }
        ps.println("\t" + current + " [label=\"" + current.field + "\", shape=circle];");
        for (Map.Entry<NDD, Long> entry : current.getEdges().entrySet()) {
            ps.println("\t" + current + " -> " + entry.getKey() + " [label=\"" + entry.getValue() + "\"];");
            printDot_rec(entry.getKey());
            printBDDDot(entry.getValue());
        }
    }

    private static void printBDDDot(long bdd) {
        if (bdd == 1 || bdd == 0) {
            return;
        }
        if (visitedBDD.containsKey(bdd)) {
            return;
        }
        visitedBDD.put(bdd, true);
        JSylvan.printDot(bdd);
    }

    /**
     * The field of the node.
     */
    protected int field;

    /**
     * All the edges of the node.
     */
    private Map<NDD, Long> edges;

    /**
     * Construct function, used for terminal nodes.
     */
    public NDD() {}

    /**
     * Construct function, used for non-terminal nodes.
     * 
     * @param field The field that the node branches on.
     * @param edges Edges of the node.
     */
    public NDD(int field, Map<NDD, Long> edges) {
        this.field = field;
        this.edges = edges;
    }

    /**
     * Get the field of the node.
     * 
     * @return The field of the node.
     */
    public int getField() {
        return field;
    }

    /**
     * Get all the edges of the node.
     * 
     * @return All the edges.
     */
    public Map<NDD, Long> getEdges() {
        return edges;
    }

    /**
     * The terminal node TRUE.
     */
    private final static NDD TRUE = new NDD();

    /**
     * The terminal node FALSE.
     */
    private final static NDD FALSE = new NDD();

    /**
     * Get the terminal node TRUE.
     * 
     * @return The terminal node TRUE.
     */
    public static NDD getTrue() {
        return TRUE;
    }

    /**
     * Get the terminal node FALSE.
     * 
     * @return The terminal node FALSE.
     */
    public static NDD getFalse() {
        return FALSE;
    }

    /**
     * Check if the node is the terminal node TRUE.
     * 
     * @return If the node is the terminal node TRUE.
     */
    public boolean isTrue() {
        return this == getTrue();
    }

    /**
     * Check if the node is the terminal node FALSE.
     * 
     * @return If the node is the terminal node FALSE.
     */
    public boolean isFalse() {
        return this == getFalse();
    }

    /**
     * Check if the node is a terminal node.
     * 
     * @return If the node is a terminal node.
     */
    public boolean isTerminal() {
        return this == getTrue() || this == getFalse();
    }

    @Override
    public boolean equals(Object ndd) {
        return this == ndd;
    }

    @Override
    public int hashCode() {
        return System.identityHashCode(this);
    }

    @Override
    public String toString() {
        return "NDD_" + System.identityHashCode(this);
    }

    // create or reuse a new NDD node
    /**
     * Create or reuse an NDD node.
     * Note that, one should ref all bdd labels in edges before invoking mk.
     * 
     * @param field The field of the ndd node.
     * @param edges All the edges of the ndd node.
     * @return The ndd node.
     */
    public static NDD mk(int field, Map<NDD, Long> edges) {
        return nodeTable.mk(field, edges);
    }

    public static int nodeCount() {
        ArrayList<Map<Map<NDD, Long>, NDD>> tables = nodeTable.getNodeTable();
        int nodeCount = 0;
        for (Map<Map<NDD, Long>, NDD> table : tables) {
            nodeCount += table.size();
        }
        return nodeCount;
    }
}
