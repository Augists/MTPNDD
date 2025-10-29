package org.ants.mtpndd;

/**
 * Optional tuning parameters that can be applied once the native runtime is initialised.
 */
public final class MtpnddAdvancedConfig {
    private final double quickGrowthThreshold;
    private final long edgeBucketCount;
    private final long nodetableBucketCount;
    private final long gcBucketCount;

    private MtpnddAdvancedConfig(Builder builder) {
        this.quickGrowthThreshold = builder.quickGrowthThreshold;
        this.edgeBucketCount = builder.edgeBucketCount;
        this.nodetableBucketCount = builder.nodetableBucketCount;
        this.gcBucketCount = builder.gcBucketCount;
    }

    public double quickGrowthThreshold() {
        return quickGrowthThreshold;
    }

    public long edgeBucketCount() {
        return edgeBucketCount;
    }

    public long nodetableBucketCount() {
        return nodetableBucketCount;
    }

    public long gcBucketCount() {
        return gcBucketCount;
    }

    public static Builder builder() {
        return new Builder();
    }

    public static final class Builder {
        private double quickGrowthThreshold = 0.5d;
        private long edgeBucketCount = 0;
        private long nodetableBucketCount = 0;
        private long gcBucketCount = 0;

        private Builder() {}

        public Builder quickGrowthThreshold(double threshold) {
            if (threshold < 0.0d) {
                throw new IllegalArgumentException("quickGrowthThreshold must be non-negative");
            }
            this.quickGrowthThreshold = threshold;
            return this;
        }

        public Builder edgeBucketCount(long count) {
            this.edgeBucketCount = validateNonNegative(count, "edgeBucketCount");
            return this;
        }

        public Builder nodetableBucketCount(long count) {
            this.nodetableBucketCount = validateNonNegative(count, "nodetableBucketCount");
            return this;
        }

        public Builder gcBucketCount(long count) {
            this.gcBucketCount = validateNonNegative(count, "gcBucketCount");
            return this;
        }

        public MtpnddAdvancedConfig build() {
            return new MtpnddAdvancedConfig(this);
        }

        private static long validateNonNegative(long value, String name) {
            if (value < 0) {
                throw new IllegalArgumentException(name + " must be >= 0: " + value);
            }
            return value;
        }
    }
}
