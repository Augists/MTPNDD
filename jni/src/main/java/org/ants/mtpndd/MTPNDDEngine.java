package org.ants.mtpndd;

import java.util.Objects;

/**
 * Runtime façade responsible for loading the native library and managing the global MTPNDD state.
 *
 * <p>The {@link MTPNDD} class models individual nodes. User code should only manipulate nodes while
 * relying on this class for global lifecycle management (initialisation, shutdown, configuration).</p>
 */
public final class MTPNDDEngine {
    private static final String LIBRARY_PROPERTY = "org.ants.mtpndd.library.path";

    static {
        loadNativeLibrary();
    }

    private MTPNDDEngine() {}

    private static synchronized void loadNativeLibrary() {
        try {
            String explicitPath = System.getProperty(LIBRARY_PROPERTY);
            if (explicitPath != null && !explicitPath.isEmpty()) {
                System.load(explicitPath);
            } else {
                System.loadLibrary("mtpnddjni");
            }
        } catch (UnsatisfiedLinkError error) {
            throw new MTPNDDException("Unable to load libmtpnddjni. "
                    + "Set -D" + LIBRARY_PROPERTY + "=<absolute path> if the library "
                    + "is not on java.library.path.", error);
        }
    }

    public static synchronized void init(MTPNDDConfig config) {
        Objects.requireNonNull(config, "config");
        double quickGrowth = config.quickGrowthThreshold();
        initNative(config.workers(),
                config.laceDequeSize(),
                config.bddNodeTableSize(),
                config.mtpnddNodeTableSize(),
                config.operationCacheSize(),
                Double.isNaN(quickGrowth) ? -1.0d : quickGrowth,
                config.edgeBucketCount(),
                config.nodetableBucketCount(),
                config.nodeSlabCapacity(),
                config.edgeEntrySlabCapacity(),
                config.nodetableEntrySlabCapacity(),
                config.edgeMapSlabCapacity());
    }

    public static synchronized void shutdown() {
        quitNative();
    }

    public static synchronized boolean isInitialized() {
        return isInitializedNative();
    }

    public static synchronized int declareField(int bitWidth) {
        return declareFieldNative(bitWidth);
    }

    public static synchronized void generateFields() {
        generateFieldsNative();
    }

    public static synchronized MTPNDDFieldInfo getFieldInfo(int fieldId) {
        return getFieldInfoNative(fieldId);
    }

    static synchronized long getBddVar(int fieldId, int index) {
        return getBddVarNative(fieldId, index);
    }

    static synchronized long getBddNotVar(int fieldId, int index) {
        return getBddNotVarNative(fieldId, index);
    }

    static synchronized long bddTrue() {
        return bddTrueNative();
    }

    static synchronized long bddFalse() {
        return bddFalseNative();
    }

    static synchronized long bddRef(long handle) {
        return bddRefNative(handle);
    }

    static synchronized void bddDeref(long handle) {
        bddDerefNative(handle);
    }

    static synchronized long bddAnd(long left, long right) {
        return bddAndNative(left, right);
    }

    static synchronized long bddOr(long left, long right) {
        return bddOrNative(left, right);
    }

    static synchronized long bddNot(long value) {
        return bddNotNative(value);
    }

    static synchronized MTPNDD fromMtbdd(long handle) {
        return wrap(fromMtbddNative(handle));
    }

    static synchronized MTPNDD getVar(int fieldId, int index) {
        return wrap(getVarNative(fieldId, index));
    }

    static synchronized MTPNDD getNotVar(int fieldId, int index) {
        return wrap(getNotVarNative(fieldId, index));
    }

    static synchronized int getFieldId(MTPNDD node) {
        Objects.requireNonNull(node, "node");
        return getFieldIdNative(node.nativePtr);
    }

    static synchronized MTPNDDEdges getEdges(MTPNDD node) {
        Objects.requireNonNull(node, "node");
        return MTPNDDEdges.fromPairs(getEdgesNative(node.nativePtr));
    }

    static synchronized void ref(MTPNDD node) {
        Objects.requireNonNull(node, "node");
        refNative(node.nativePtr);
    }

    static synchronized void deref(MTPNDD node) {
        Objects.requireNonNull(node, "node");
        derefNative(node.nativePtr);
    }

    static synchronized MTPNDD and(MTPNDD left, MTPNDD right) {
        Objects.requireNonNull(left, "left");
        Objects.requireNonNull(right, "right");
        return wrap(andNative(left.nativePtr, right.nativePtr));
    }

    static synchronized MTPNDD or(MTPNDD left, MTPNDD right) {
        Objects.requireNonNull(left, "left");
        Objects.requireNonNull(right, "right");
        return wrap(orNative(left.nativePtr, right.nativePtr));
    }

    static synchronized MTPNDD not(MTPNDD value) {
        Objects.requireNonNull(value, "value");
        return wrap(notNative(value.nativePtr));
    }

    static synchronized MTPNDD diff(MTPNDD left, MTPNDD right) {
        Objects.requireNonNull(left, "left");
        Objects.requireNonNull(right, "right");
        return wrap(diffNative(left.nativePtr, right.nativePtr));
    }

    static synchronized MTPNDD exist(MTPNDD value, int fieldId) {
        Objects.requireNonNull(value, "value");
        return wrap(existNative(value.nativePtr, fieldId));
    }

    static synchronized double satCount(MTPNDD value) {
        Objects.requireNonNull(value, "value");
        return satCountNative(value.nativePtr);
    }

    static synchronized int minZeros(MTPNDD value) {
        Objects.requireNonNull(value, "value");
        return minZerosNative(value.nativePtr);
    }

    static synchronized MTPNDD getTrue() {
        return wrap(terminalTrueNative());
    }

    static synchronized MTPNDD getFalse() {
        return wrap(terminalFalseNative());
    }

    static synchronized boolean isTrue(MTPNDD node) {
        Objects.requireNonNull(node, "node");
        return isTrueNative(node.nativePtr);
    }

    static synchronized boolean isFalse(MTPNDD node) {
        Objects.requireNonNull(node, "node");
        return isFalseNative(node.nativePtr);
    }

    static synchronized boolean isTerminal(MTPNDD node) {
        Objects.requireNonNull(node, "node");
        return isTerminalNative(node.nativePtr);
    }

    public static synchronized boolean bddIsTrue(long handle) {
        return bddIsTrueNative(handle);
    }

    public static synchronized boolean bddIsFalse(long handle) {
        return bddIsFalseNative(handle);
    }

    public static synchronized int bddVar(long handle) {
        return bddVarNative(handle);
    }

    public static synchronized long bddLow(long handle) {
        return bddLowNative(handle);
    }

    public static synchronized long bddHigh(long handle) {
        return bddHighNative(handle);
    }

    public static synchronized MTPNDDStats stats() {
        return getStatsNative();
    }

    private static MTPNDD wrap(long nativePtr) {
        return new MTPNDD(nativePtr);
    }

    private static native void initNative(int workers,
                                          long laceDequeSize,
                                          long bddNodeTableSize,
                                          long mtpnddNodeTableSize,
                                          long operationCacheSize,
                                          double quickGrowthThreshold,
                                          long edgeBucketCount,
                                          long nodetableBucketCount,
                                          long nodeSlabCapacity,
                                          long edgeEntrySlabCapacity,
                                          long nodetableEntrySlabCapacity,
                                          long edgeMapSlabCapacity);

    private static native void quitNative();

    private static native boolean isInitializedNative();

    private static native int declareFieldNative(int bitWidth);
    private static native void generateFieldsNative();

    private static native MTPNDDFieldInfo getFieldInfoNative(int fieldId);

    private static native long getVarNative(int fieldId, int index);

    private static native long getNotVarNative(int fieldId, int index);
    private static native int getFieldIdNative(long handle);
    private static native long[] getEdgesNative(long handle);

    private static native void refNative(long handle);

    private static native void derefNative(long handle);

    private static native long andNative(long left, long right);

    private static native long orNative(long left, long right);

    private static native long notNative(long handle);

    private static native long diffNative(long left, long right);

    private static native long existNative(long handle, int fieldId);

    private static native double satCountNative(long handle);
    private static native int minZerosNative(long handle);

    private static native long terminalTrueNative();

    private static native long terminalFalseNative();

    private static native boolean isTrueNative(long handle);

    private static native boolean isFalseNative(long handle);

    private static native boolean isTerminalNative(long handle);

    private static native long getBddVarNative(int fieldId, int index);
    private static native long getBddNotVarNative(int fieldId, int index);
    private static native long bddTrueNative();
    private static native long bddFalseNative();
    private static native long bddRefNative(long handle);
    private static native void bddDerefNative(long handle);
    private static native long bddAndNative(long left, long right);
    private static native long bddOrNative(long left, long right);
    private static native long bddNotNative(long value);
    private static native long fromMtbddNative(long handle);
    private static native boolean bddIsTrueNative(long handle);
    private static native boolean bddIsFalseNative(long handle);
    private static native int bddVarNative(long handle);
    private static native long bddLowNative(long handle);
    private static native long bddHighNative(long handle);

    private static native MTPNDDStats getStatsNative();
}
