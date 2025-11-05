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
                config.gcBucketCount(),
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

    public static synchronized MTPNDDFieldInfo getFieldInfo(int fieldId) {
        return getFieldInfoNative(fieldId);
    }

    static synchronized MTPNDD getVar(int fieldId, int index) {
        return wrap(getVarNative(fieldId, index));
    }

    static synchronized MTPNDD getNotVar(int fieldId, int index) {
        return wrap(getNotVarNative(fieldId, index));
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

    static synchronized MTPNDD terminalTrue() {
        return wrap(terminalTrueNative());
    }

    static synchronized MTPNDD terminalFalse() {
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
                                          long gcBucketCount,
                                          long nodeSlabCapacity,
                                          long edgeEntrySlabCapacity,
                                          long nodetableEntrySlabCapacity,
                                          long edgeMapSlabCapacity);

    private static native void quitNative();

    private static native boolean isInitializedNative();

    private static native int declareFieldNative(int bitWidth);

    private static native MTPNDDFieldInfo getFieldInfoNative(int fieldId);

    private static native long getVarNative(int fieldId, int index);

    private static native long getNotVarNative(int fieldId, int index);

    private static native long andNative(long left, long right);

    private static native long orNative(long left, long right);

    private static native long notNative(long handle);

    private static native long diffNative(long left, long right);

    private static native long existNative(long handle, int fieldId);

    private static native double satCountNative(long handle);

    private static native long terminalTrueNative();

    private static native long terminalFalseNative();

    private static native boolean isTrueNative(long handle);

    private static native boolean isFalseNative(long handle);

    private static native boolean isTerminalNative(long handle);

    private static native MTPNDDStats getStatsNative();
}
