package org.ants.mtpndd;

import java.util.Objects;

/**
 * Thin JNI wrapper around the native runtime.
 *
 * <p>All methods here are {@code synchronized} to provide coarse-grained safety around the
 * underlying global state. More fine-grained coordination should be performed by callers if
 * needed.</p>
 */
public final class MtpnddNative {
    private static final String LIBRARY_PROPERTY = "org.ants.mtpndd.library.path";

    static {
        loadNativeLibrary();
    }

    private static synchronized void loadNativeLibrary() {
        try {
            String explicitPath = System.getProperty(LIBRARY_PROPERTY);
            if (explicitPath != null && !explicitPath.isEmpty()) {
                System.load(explicitPath);
            } else {
                System.loadLibrary("mtpnddjni");
            }
        } catch (UnsatisfiedLinkError error) {
            throw new MtpnddException("Unable to load libmtpnddjni. "
                    + "Set -D" + LIBRARY_PROPERTY + "=<absolute path> if the library "
                    + "is not on java.library.path.", error);
        }
    }

    private MtpnddNative() {}

    public static synchronized void init(MtpnddConfig config) {
        Objects.requireNonNull(config, "config");
        initNative(config.workers(),
                config.laceDequeSize(),
                config.bddNodeTableSize(),
                config.mtpnddNodeTableSize(),
                config.operationCacheSize());
    }

    public static synchronized void applyAdvancedConfig(MtpnddAdvancedConfig config) {
        Objects.requireNonNull(config, "config");
        configureAdvancedNative(config.quickGrowthThreshold(),
                config.edgeBucketCount(),
                config.nodetableBucketCount(),
                config.gcBucketCount());
    }

    public static synchronized void shutdown() {
        quitNative();
    }

    public static synchronized boolean isInitialized() {
        return isInitializedNative();
    }

    public static synchronized String getLastError() {
        return getLastErrorNative();
    }

    private static native void initNative(int workers,
                                          long laceDequeSize,
                                          long bddNodeTableSize,
                                          long mtpnddNodeTableSize,
                                          long operationCacheSize);

    private static native void configureAdvancedNative(double quickGrowthThreshold,
                                                       long edgeBucketCount,
                                                       long nodetableBucketCount,
                                                       long gcBucketCount);

    private static native void quitNative();

    private static native boolean isInitializedNative();

    private static native String getLastErrorNative();
}
