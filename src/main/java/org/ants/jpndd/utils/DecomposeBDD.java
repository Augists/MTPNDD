/**
 * Utility for decomposing NDD.
 * @author Zechun Li - XJTU ANTS NetVerify Lab
 * @version 1.0
 */
package org.ants.jpndd.utils;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;

import jsylvan.JSylvan;

public class DecomposeBDD {
    private final static long BDD_FALSE = JSylvan.getFalse();
    private final static long BDD_TRUE = JSylvan.getTrue();
    private static int fieldNum;
    private static ArrayList<Integer> maxVariablePerField;

    public static HashMap<Long, HashMap<Long, Long>> decompose(long a, ArrayList<Integer> vars) {
        maxVariablePerField = vars;
        fieldNum = maxVariablePerField.size();
        HashMap<Long, HashMap<Long, Long>> decomposedBDD = new HashMap<>();
        if (a == BDD_FALSE) {
        } else if (a == BDD_TRUE) {
            HashMap<Long, Long> map = new HashMap<>();
            map.put(BDD_TRUE, BDD_TRUE);
            decomposedBDD.put(BDD_TRUE, map);
        } else {
            HashMap<Long, HashSet<Long>> boundaryTree = new HashMap<>();
            ArrayList<HashSet<Long>> boundaryPoints = new ArrayList<>();
            getBoundaryTree(a, boundaryTree, boundaryPoints);

            for (int currentField = 0; currentField < fieldNum - 1; currentField++) {
                for (long from : boundaryPoints.get(currentField)) {
                    decomposedBDD.put(from, new HashMap<>());
                    for (long to : boundaryTree.get(from)) {
                        long perFieldBDD = JSylvan.ref(constructPerFieldBDD(from, to, from));
                        decomposedBDD.get(from).put(to, perFieldBDD);
                    }
                }
            }

            for (long from : boundaryPoints.get(fieldNum - 1)) {
                decomposedBDD.put(from, new HashMap<>());
                decomposedBDD.get(from).put(BDD_TRUE, JSylvan.ref(from));
            }
        }
        return decomposedBDD;
    }

    public static int bddGetField(long a) {
        if (a == BDD_FALSE || a == BDD_TRUE) {
            return fieldNum;
        }
        int varA = JSylvan.getVar(a);
        int currentField = 0;
        while (currentField < fieldNum) {
            if (varA <= maxVariablePerField.get(currentField)) {
                break;
            }
            currentField++;
        }
        return currentField;
    }

    private static void getBoundaryTree(long a, HashMap<Long, HashSet<Long>> boundaryTree,
                                        ArrayList<HashSet<Long>> boundaryPoints) {
        int startField = bddGetField(a);
        for (int i = 0; i < fieldNum; i++) {
            boundaryPoints.add(new HashSet<>());
        }
        boundaryPoints.get(startField).add(a);
        if (startField == fieldNum - 1) {
            boundaryTree.put(a, new HashSet<>());
            boundaryTree.get(a).add(BDD_TRUE);
        } else {
            for (int currentField = startField; currentField < fieldNum; currentField++) {
                for (long from : boundaryPoints.get(currentField)) {
                    detectBoundaryPoints(from, from, boundaryTree, boundaryPoints);
                }
            }
        }
    }

    private static void detectBoundaryPoints(long from, long current, HashMap<Long, HashSet<Long>> boundaryTree,
                                             ArrayList<HashSet<Long>> boundaryPoints) {
        if (current == BDD_FALSE) {
            return;
        }

        if (bddGetField(from) != bddGetField(current)) {
            if (!boundaryTree.containsKey(from)) {
                boundaryTree.put(from, new HashSet<>());
            }
            boundaryTree.get(from).add(current);
            if (current != BDD_TRUE) {
                boundaryPoints.get(bddGetField(current)).add(current);
            }
            return;
        }

        detectBoundaryPoints(from, JSylvan.getThen(current), boundaryTree, boundaryPoints);
        detectBoundaryPoints(from, JSylvan.getElse(current), boundaryTree, boundaryPoints);
    }

    // return per field bdd without ref
    private static long constructPerFieldBDD(long from, long to, long current) {
        if (bddGetField(from) != bddGetField(current)) {
            if (to == current)
                return BDD_TRUE;
            else
                return BDD_FALSE;
        }

        long new_low = JSylvan.ref(constructPerFieldBDD(from, to, JSylvan.getThen(current)));
        long new_high = JSylvan.ref(constructPerFieldBDD(from, to, JSylvan.getElse(current)));
        long result = JSylvan.makeIte(JSylvan.getVar(current), new_low, new_high);
        JSylvan.deref(new_low);
        JSylvan.deref(new_high);
        return result;
    }
}
