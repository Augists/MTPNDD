package org.ants.mtpndd;

import java.util.Objects;

/**
 * Configuration parameters used while initialising the native MTPNDD runtime.
 */
public final class MtpnddConfig {
    private final int workers;
    private final long laceDequeSize;
    private final long bddNodeTableSize;
    private final long mtpnddNodeTableSize;
    private final long operationCacheSize;

    private MtpnddConfig(Builder builder) {
        this.workers = builder.workers;
        this.laceDequeSize = builder.laceDequeSize;
        this.bddNodeTableSize = builder.bddNodeTableSize;
        this.mtpnddNodeTableSize = builder.mtpnddNodeTableSize;
        this.operationCacheSize = builder.operationCacheSize;
    }

    public int workers() {
        return workers;
    }

    public long laceDequeSize() {
        return laceDequeSize;
    }

    public long bddNodeTableSize() {
        return bddNodeTableSize;
    }

    public long mtpnddNodeTableSize() {
        return mtpnddNodeTableSize;
    }

    public long operationCacheSize() {
        return operationCacheSize;
    }

    public static Builder builder() {
        return new Builder();
    }

    /**
     * Builder exposing the handful of sizing parameters required for the PAL.
     */
    public static final class Builder {
        private int workers = Runtime.getRuntime().availableProcessors();
        private long laceDequeSize = 4096L;
        private long bddNodeTableSize = 1L << 24;
        private long mtpnddNodeTableSize = 1L << 22;
        private long operationCacheSize = 1L << 20;

        private Builder() {}

        public Builder workers(int workers) {
            if (workers <= 0) {
                throw new IllegalArgumentException("workers must be positive");
            }
            this.workers = workers;
            return this;
        }

        public Builder laceDequeSize(long laceDequeSize) {
            this.laceDequeSize = validatePositive(laceDequeSize, "laceDequeSize");
            return this;
        }

        public Builder bddNodeTableSize(long size) {
            this.bddNodeTableSize = validatePositive(size, "bddNodeTableSize");
            return this;
        }

        public Builder mtpnddNodeTableSize(long size) {
            this.mtpnddNodeTableSize = validatePositive(size, "mtpnddNodeTableSize");
            return this;
        }

        public Builder operationCacheSize(long size) {
            this.operationCacheSize = validatePositive(size, "operationCacheSize");
            return this;
        }

        public MtpnddConfig build() {
            return new MtpnddConfig(this);
        }

        private static long validatePositive(long value, String name) {
            if (value <= 0) {
                throw new IllegalArgumentException(name + " must be positive: " + value);
            }
            return value;
        }
    }

    @Override
    public String toString() {
        return "MtpnddConfig{" +
                "workers=" + workers +
                ", laceDequeSize=" + laceDequeSize +
                ", bddNodeTableSize=" + bddNodeTableSize +
                ", mtpnddNodeTableSize=" + mtpnddNodeTableSize +
                ", operationCacheSize=" + operationCacheSize +
                '}';
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof MtpnddConfig)) {
            return false;
        }
        MtpnddConfig that = (MtpnddConfig) o;
        return workers == that.workers
                && laceDequeSize == that.laceDequeSize
                && bddNodeTableSize == that.bddNodeTableSize
                && mtpnddNodeTableSize == that.mtpnddNodeTableSize
                && operationCacheSize == that.operationCacheSize;
    }

    @Override
    public int hashCode() {
        return Objects.hash(workers, laceDequeSize, bddNodeTableSize, mtpnddNodeTableSize, operationCacheSize);
    }
}
