package org.ants.mtpndd;

import java.util.Objects;

/**
 * Immutable handle to an MTPNDD node exposed to Java callers.
 *
 * <p>Instances are thin wrappers around native pointers. All node-transforming operations create
 * new {@link MTPNDD} instances; original nodes remain valid after the call.</p>
 */
public final class MTPNDD {
    final long nativePtr;

    MTPNDD(long nativePointer) {
        if (nativePointer == 0L) {
            throw new MTPNDDException("Native runtime returned an empty MTPNDD pointer.");
        }
        this.nativePtr = nativePointer;
    }

    /**
     * Return the node representing the specified positive literal.
     */
    public static MTPNDD getVar(int fieldId, int index) {
        return MTPNDDEngine.getVar(fieldId, index);
    }

    /**
     * Return the node representing the negated literal.
     */
    public static MTPNDD getNotVar(int fieldId, int index) {
        return MTPNDDEngine.getNotVar(fieldId, index);
    }

    /**
     * Global constant for the logical {@code true}.
     */
    public static MTPNDD terminalTrue() {
        return MTPNDDEngine.terminalTrue();
    }

    /**
     * Global constant for the logical {@code false}.
     */
    public static MTPNDD terminalFalse() {
        return MTPNDDEngine.terminalFalse();
    }

    public MTPNDD and(MTPNDD other) {
        return MTPNDDEngine.and(this, other);
    }

    public MTPNDD or(MTPNDD other) {
        return MTPNDDEngine.or(this, other);
    }

    public MTPNDD diff(MTPNDD other) {
        return MTPNDDEngine.diff(this, other);
    }

    public MTPNDD not() {
        return MTPNDDEngine.not(this);
    }

    public MTPNDD exist(int fieldId) {
        return MTPNDDEngine.exist(this, fieldId);
    }

    public double satCount() {
        return MTPNDDEngine.satCount(this);
    }

    public boolean isTrue() {
        return MTPNDDEngine.isTrue(this);
    }

    public boolean isFalse() {
        return MTPNDDEngine.isFalse(this);
    }

    public boolean isTerminal() {
        return MTPNDDEngine.isTerminal(this);
    }

    /**
     * Return the field id for this node (0 for terminal nodes).
     */
    public int getField() {
        return MTPNDDEngine.getField(this);
    }

    /**
     * Return all outgoing edges of this node.
     */
    public MTPNDDEdges getEdges() {
        return MTPNDDEngine.getEdges(this);
    }

    /**
     * Compute the minimum number of zeros needed to reach TRUE in this node.
     */
    public int minZeros() {
        return MTPNDDEngine.minZeros(this);
    }

    /**
     * Increment the native reference count for this node.
     */
    public MTPNDD ref() {
        MTPNDDEngine.ref(this);
        return this;
    }

    /**
     * Decrement the native reference count for this node.
     */
    public void deref() {
        MTPNDDEngine.deref(this);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof MTPNDD)) {
            return false;
        }
        MTPNDD mtpndd = (MTPNDD)o;
        return nativePtr == mtpndd.nativePtr;
    }

    @Override
    public int hashCode() {
        return Long.hashCode(nativePtr);
    }

    @Override
    public String toString() {
        return "MTPNDD{nativePtr=0x" + Long.toHexString(nativePtr) + '}';
    }
}
