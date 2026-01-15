package org.ants.mtpndd;

import java.util.Iterator;
import java.util.NoSuchElementException;

/**
 * Container for enumerating all edges of an MTPNDD node.
 */
public final class MTPNDDEdges implements Iterable<MTPNDDEdge> {
    private final long[] childHandles;
    private final long[] predicateHandles;

    MTPNDDEdges(long[] childHandles, long[] predicateHandles) {
        this.childHandles = childHandles == null ? new long[0] : childHandles;
        this.predicateHandles = predicateHandles == null ? new long[0] : predicateHandles;
        if (this.childHandles.length != this.predicateHandles.length) {
            throw new IllegalArgumentException("Child/predicate handle arrays must be the same length.");
        }
    }

    public int size() {
        return childHandles.length;
    }

    public boolean isEmpty() {
        return childHandles.length == 0;
    }

    public MTPNDD childAt(int index) {
        return MTPNDDEngine.wrap(childHandles[index]);
    }

    public long predicateAt(int index) {
        return predicateHandles[index];
    }

    public MTPNDDEdge edgeAt(int index) {
        return new MTPNDDEdge(childAt(index), predicateAt(index));
    }

    @Override
    public Iterator<MTPNDDEdge> iterator() {
        return new Iterator<MTPNDDEdge>() {
            private int index = 0;

            @Override
            public boolean hasNext() {
                return index < childHandles.length;
            }

            @Override
            public MTPNDDEdge next() {
                if (!hasNext()) {
                    throw new NoSuchElementException("No more edges.");
                }
                return edgeAt(index++);
            }
        };
    }
}
