package org.ants.mtpndd;

public final class NQueensMTPNDD {
    private NQueensMTPNDD() {}

    public static final class Result {
        public final int n;
        public final double seconds;
        public final long solutions;

        Result(int n, double seconds, long solutions) {
            this.n = n;
            this.seconds = seconds;
            this.solutions = solutions;
        }

        @Override
        public String toString() {
            return "Result{n=" + n + ", seconds=" + seconds + ", solutions=" + solutions + '}';
        }
    }

    private static int ceilLog2(int value) {
        if (value <= 1) {
            return 1;
        }
        int highest = Integer.highestOneBit(value - 1);
        return Integer.numberOfTrailingZeros(highest) + 1;
    }

    public static Result solve(int n) {
        if (n <= 0) {
            throw new IllegalArgumentException("n must be positive");
        }
        int bitWidth = ceilLog2(n);
        MTPNDDConfig config = MTPNDDConfig.builder()
                .workers(1)
                .laceDequeSize(1 << 16)
                .bddNodeTableSize(1 << 24)
                .mtpnddNodeTableSize(1 << 22)
                .operationCacheSize(1 << 20)
                .edgeBucketCount(256)
                .nodetableBucketCount(1 << 20)
                .gcBucketCount(1 << 20)
                .nodeSlabCapacity(2048)
                .edgeEntrySlabCapacity(4096)
                .nodetableEntrySlabCapacity(2048)
                .edgeMapSlabCapacity(2048)
                .build();
        long start = System.nanoTime();
        MTPNDDEngine.init(config);
        try {
            int[] fieldIds = new int[n];
            for (int i = 0; i < n; i++) {
                fieldIds[i] = MTPNDDEngine.declareField(bitWidth);
            }

            MTPNDD[][] positiveBits = new MTPNDD[n][bitWidth];
            MTPNDD[][] negativeBits = new MTPNDD[n][bitWidth];
            for (int row = 0; row < n; row++) {
                for (int bit = 0; bit < bitWidth; bit++) {
                    positiveBits[row][bit] = MTPNDD.var(fieldIds[row], bit);
                    negativeBits[row][bit] = MTPNDD.notVar(fieldIds[row], bit);
                }
            }

            MTPNDD[][] eqCache = new MTPNDD[n][n];
            for (int row = 0; row < n; row++) {
                for (int value = 0; value < n; value++) {
                    eqCache[row][value] = buildEquality(row, value, bitWidth, positiveBits, negativeBits);
                }
            }

            MTPNDD formula = MTPNDD.terminalTrue();
            for (int row = 0; row < n; row++) {
                MTPNDD domain = buildRowDomain(row, n, bitWidth, positiveBits, negativeBits);
                formula = formula.and(domain);
            }

            for (int row = 0; row < n; row++) {
                MTPNDD atLeastOne = buildRowAtLeastOne(eqCache[row]);
                formula = formula.and(atLeastOne);
            }

            for (int a = 0; a < n; a++) {
                for (int b = a + 1; b < n; b++) {
                    for (int col = 0; col < n; col++) {
                        MTPNDD clause = forbidPair(eqCache, a, col, b, col);
                        formula = formula.and(clause);
                    }
                    int delta = b - a;
                    for (int col = 0; col < n; col++) {
                        int other = col + delta;
                        if (other < n) {
                            MTPNDD clause = forbidPair(eqCache, a, col, b, other);
                            formula = formula.and(clause);
                        }
                        other = col - delta;
                        if (other >= 0) {
                            MTPNDD clause = forbidPair(eqCache, a, col, b, other);
                            formula = formula.and(clause);
                        }
                    }
                }
            }

            double solutions = formula.satCount();

            double elapsedSeconds = (System.nanoTime() - start) / 1_000_000_000.0;
            return new Result(n, elapsedSeconds, Math.round(solutions));
        } finally {
            MTPNDDEngine.shutdown();
        }
    }

    private static MTPNDD buildEquality(int row, int value, int bitWidth,
                                        MTPNDD[][] positiveBits,
                                        MTPNDD[][] negativeBits) {
        MTPNDD result = MTPNDD.terminalTrue();
        for (int bit = 0; bit < bitWidth; bit++) {
            boolean bitSet = ((value >> bit) & 1) != 0;
            MTPNDD literal = bitSet ? positiveBits[row][bit] : negativeBits[row][bit];
            result = result.and(literal);
        }
        return result;
    }

    private static MTPNDD buildRowDomain(int row, int n, int bitWidth,
                                         MTPNDD[][] positiveBits,
                                         MTPNDD[][] negativeBits) {
        int limit = 1 << bitWidth;
        MTPNDD domain = MTPNDD.terminalTrue();
        for (int value = n; value < limit; value++) {
            MTPNDD eq = buildEquality(row, value, bitWidth, positiveBits, negativeBits);
            domain = domain.and(eq.not());
        }
        return domain;
    }

    private static MTPNDD buildRowAtLeastOne(MTPNDD[] rowValues) {
        MTPNDD condition = MTPNDD.terminalFalse();
        for (MTPNDD handle : rowValues) {
            condition = condition.or(handle);
        }
        return condition;
    }

    private static MTPNDD forbidPair(MTPNDD[][] eqCache, int rowA, int valueA, int rowB, int valueB) {
        MTPNDD both = eqCache[rowA][valueA].and(eqCache[rowB][valueB]);
        return both.not();
    }

    public static void main(String[] args) {
        int n = args.length > 0 ? Integer.parseInt(args[0]) : 8;
        Result result = solve(n);
        System.out.printf("n=%d solutions=%d time=%.3fs%n", result.n, result.solutions, result.seconds);
    }
}
