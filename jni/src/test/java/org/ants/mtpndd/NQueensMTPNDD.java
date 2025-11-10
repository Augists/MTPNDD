package org.ants.mtpndd;

import java.util.LinkedHashMap;
import java.util.Map;

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

    public static Result solve(int n) {
        return solveBinary(n);
    }

    public static Result solveBinary(int n) {
        if (n <= 0) {
            throw new IllegalArgumentException("n must be positive");
        }

        Timings timings = new Timings();
        timings.mark("start");
        MTPNDDConfig config = buildConfigForSize(n);
        MTPNDDEngine.init(config);
        timings.mark("init");

        try {
            int bitWidth = ceilLog2(n);
            System.out.println("bitwidth: " + bitWidth);
            int[] fieldIds = new int[n];
            for (int i = 0; i < n; i++) {
                fieldIds[i] = MTPNDDEngine.declareField(bitWidth);
            }
            timings.mark("declare");

            MTPNDD[][] positiveBits = new MTPNDD[n][bitWidth];
            MTPNDD[][] negativeBits = new MTPNDD[n][bitWidth];
            for (int row = 0; row < n; row++) {
                for (int bit = 0; bit < bitWidth; bit++) {
                    positiveBits[row][bit] = MTPNDD.getVar(fieldIds[row], bit);
                    negativeBits[row][bit] = MTPNDD.getNotVar(fieldIds[row], bit);
                }
            }

            MTPNDD[][] eqCache = new MTPNDD[n][n];
            for (int row = 0; row < n; row++) {
                for (int value = 0; value < n; value++) {
                    eqCache[row][value] = buildEquality(row, value, bitWidth, positiveBits, negativeBits);
                }
            }
            timings.mark("cache");

            MTPNDD formula = MTPNDD.terminalTrue();
            for (int row = 0; row < n; row++) {
                MTPNDD domain = buildRowDomain(row, n, bitWidth, positiveBits, negativeBits);
                formula = andRelease(formula, domain);
            }
            for (int row = 0; row < n; row++) {
                MTPNDD atLeastOne = buildRowAtLeastOne(eqCache[row]);
                formula = andRelease(formula, atLeastOne);
            }
            for (int a = 0; a < n; a++) {
                for (int b = a + 1; b < n; b++) {
                    for (int col = 0; col < n; col++) {
                        MTPNDD clause = forbidPair(eqCache, a, col, b, col);
                        formula = andRelease(formula, clause);
                    }
                    int delta = b - a;
                    for (int col = 0; col < n; col++) {
                        int other = col + delta;
                        if (other < n) {
                            MTPNDD clause = forbidPair(eqCache, a, col, b, other);
                            formula = andRelease(formula, clause);
                        }
                        other = col - delta;
                        if (other >= 0) {
                            MTPNDD clause = forbidPair(eqCache, a, col, b, other);
                            formula = andRelease(formula, clause);
                        }
                    }
                }
            }

            double solutions = formula.satCount();
            formula.deref();
            releaseCache(eqCache);
            timings.mark("satcount");
            printStats("binary", n);
            logTimings("binary", n, timings);
            return new Result(n, timings.elapsedSeconds(), Math.round(solutions));
        } finally {
            MTPNDDEngine.shutdown();
        }
    }

    public static Result solveOneHot(int n) {
        if (n <= 0) {
            throw new IllegalArgumentException("n must be positive");
        }

        Timings timings = new Timings();
        timings.mark("start");
        MTPNDDConfig config = buildConfigForSize(n);
        MTPNDDEngine.init(config);
        timings.mark("init");

        try {
            int[] fieldIds = new int[n];
            for (int i = 0; i < n; i++) {
                fieldIds[i] = MTPNDDEngine.declareField(n);
            }
            timings.mark("declare");

            MTPNDD[][] vars = new MTPNDD[n][n];
            MTPNDD[][] notVars = new MTPNDD[n][n];
            for (int row = 0; row < n; row++) {
                for (int col = 0; col < n; col++) {
                    vars[row][col] = MTPNDD.getVar(fieldIds[row], col);
                    notVars[row][col] = MTPNDD.getNotVar(fieldIds[row], col);
                }
            }

            MTPNDD[] rowRequirements = new MTPNDD[n];
            for (int row = 0; row < n; row++) {
                MTPNDD clause = MTPNDD.terminalFalse().ref();
                for (int col = 0; col < n; col++) {
                    clause = orRelease(clause, vars[row][col].ref());
                }
                rowRequirements[row] = clause;
            }

            MTPNDD[][] cellConstraints = new MTPNDD[n][n];
            for (int row = 0; row < n; row++) {
                for (int col = 0; col < n; col++) {
                    cellConstraints[row][col] = buildCellConstraints(row, col, n, vars, notVars);
                }
            }
            timings.mark("cache");

            MTPNDD formula = MTPNDD.terminalTrue().ref();
            for (MTPNDD requirement : rowRequirements) {
                formula = andRelease(formula, requirement.ref());
            }
            for (int row = 0; row < n; row++) {
                for (int col = 0; col < n; col++) {
                    formula = andRelease(formula, cellConstraints[row][col].ref());
                }
            }

            double solutions = formula.satCount();
            formula.deref();
            releaseCache(cellConstraints);
            releaseArray(rowRequirements);
            timings.mark("satcount");
            printStats("onehot", n);
            logTimings("onehot", n, timings);
            return new Result(n, timings.elapsedSeconds(), Math.round(solutions));
        } finally {
            MTPNDDEngine.shutdown();
        }
    }

    private static MTPNDDConfig buildConfigForSize(int n) {
        long bddSize = (n <= 7) ? (1L << 19) : (n <= 9) ? (1L << 22) : (1L << 25);
        long nddSize = (n <= 7) ? (1L << 18) : (n <= 9) ? (1L << 20) : (1L << 23);
        long cacheSize = (n <= 7) ? (1L << 18) : (n <= 9) ? (1L << 20) : (1L << 23);

        long edgeBuckets = (n <= 7) ? 32 : (n <= 9) ? 128 : 512;
        long nodetableBuckets = nddSize;
        long gcBuckets = (n <= 7) ? 512 : (n <= 9) ? 2048 : 8192;
        long nodeSlab = (n <= 7) ? 1024 : (n <= 9) ? 2048 : 4096;
        long edgeEntrySlab = (n <= 7) ? 2048 : (n <= 9) ? 4096 : 8192;
        long nodetableEntrySlab = (n <= 7) ? 1024 : (n <= 9) ? 2048 : 4096;
        long edgeMapSlab = (n <= 7) ? 512 : (n <= 9) ? 1024 : 2048;

        return MTPNDDConfig.builder()
                .workers(0)
                .laceDequeSize(1024)
                .bddNodeTableSize(bddSize)
                .mtpnddNodeTableSize(nddSize)
                .operationCacheSize(cacheSize)
                .quickGrowthThreshold(0.1d)
                .edgeBucketCount(edgeBuckets)
                .nodetableBucketCount(nodetableBuckets)
                .gcBucketCount(gcBuckets)
                .nodeSlabCapacity(nodeSlab)
                .edgeEntrySlabCapacity(edgeEntrySlab)
                .nodetableEntrySlabCapacity(nodetableEntrySlab)
                .edgeMapSlabCapacity(edgeMapSlab)
                .build();
    }

    private static void logTimings(String label, int n, Timings timings) {
        System.out.printf("[%s n=%d] timings (s): init=%.3f declare=%.3f cache=%.3f satcount=%.3f total=%.3f%n",
                label, n,
                timings.duration("start", "init"),
                timings.duration("init", "declare"),
                timings.duration("declare", "cache"),
                timings.duration("cache", "satcount"),
                timings.elapsedSeconds());
    }

    private static void printStats(String label, int n) {
        MTPNDDStats stats = MTPNDDEngine.stats();
        if (stats == null) {
            System.out.printf("[%s n=%d] stats unavailable (recording disabled)%n", label, n);
            return;
        }
        System.out.printf("[%s n=%d] nodeCount=%d recording=%s%n",
                label, n, stats.nodeCount(), stats.recordingEnabled());
        if (!stats.recordingEnabled()) {
            return;
        }
        System.out.printf("[%s n=%d] nodesCreated=%d reused=%d collectedLast=%d maxEdges=%d cacheHit=%d miss=%d%n",
                label, n,
                stats.nodesCreatedTotal(),
                stats.nodesReusedTotal(),
                stats.nodesCollectedLast(),
                stats.maxEdgesPerNode(),
                stats.cacheHits(),
                stats.cacheMisses());
    }

    private static MTPNDD buildEquality(int row, int value, int bitWidth,
                                        MTPNDD[][] positiveBits,
                                        MTPNDD[][] negativeBits) {
        MTPNDD result = MTPNDD.terminalTrue();
        for (int bit = 0; bit < bitWidth; bit++) {
            boolean bitSet = ((value >> bit) & 1) != 0;
            MTPNDD literal = (bitSet ? positiveBits[row][bit] : negativeBits[row][bit]).ref();
            result = andRelease(result, literal);
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
            MTPNDD neg = eq.not();
            eq.deref();
            neg.ref();
            domain = andRelease(domain, neg);
        }
        return domain;
    }

    private static MTPNDD buildRowAtLeastOne(MTPNDD[] rowValues) {
        MTPNDD condition = MTPNDD.terminalFalse();
        for (MTPNDD handle : rowValues) {
            condition = orRelease(condition, handle.ref());
        }
        return condition;
    }

    private static MTPNDD forbidPair(MTPNDD[][] eqCache, int rowA, int valueA, int rowB, int valueB) {
        MTPNDD left = eqCache[rowA][valueA].ref();
        MTPNDD right = eqCache[rowB][valueB].ref();
        MTPNDD both = andRelease(left, right);
        MTPNDD clause = both.not().ref();
        both.deref();
        return clause;
    }

    private static MTPNDD buildCellConstraints(int row, int col, int n,
                                               MTPNDD[][] vars,
                                               MTPNDD[][] notVars) {
        MTPNDD guard = vars[row][col].ref();
        MTPNDD constraints = MTPNDD.terminalTrue();

        for (int otherCol = 0; otherCol < n; otherCol++) {
            if (otherCol != col) {
                MTPNDD clause = implies(guard, notVars[row][otherCol]);
                constraints = andRelease(constraints, clause);
            }
        }
        for (int otherRow = 0; otherRow < n; otherRow++) {
            if (otherRow != row) {
                MTPNDD clause = implies(guard, notVars[otherRow][col]);
                constraints = andRelease(constraints, clause);
            }
        }
        for (int otherRow = 0; otherRow < n; otherRow++) {
            if (otherRow == row) {
                continue;
            }
            int diagRight = col + (otherRow - row);
            if (diagRight >= 0 && diagRight < n) {
                MTPNDD clause = implies(guard, notVars[otherRow][diagRight]);
                constraints = andRelease(constraints, clause);
            }
            int diagLeft = col - (otherRow - row);
            if (diagLeft >= 0 && diagLeft < n) {
                MTPNDD clause = implies(guard, notVars[otherRow][diagLeft]);
                constraints = andRelease(constraints, clause);
            }
        }

        guard.deref();
        return constraints;
    }

    private static MTPNDD implies(MTPNDD premise, MTPNDD consequence) {
        MTPNDD premiseRef = premise.ref();
        MTPNDD negatedPremise = premiseRef.not();
        premiseRef.deref();
        negatedPremise.ref();
        MTPNDD consequenceRef = consequence.ref();
        return orRelease(negatedPremise, consequenceRef);
    }

    private static int ceilLog2(int value) {
        if (value <= 1) {
            return 1;
        }
        int highest = Integer.highestOneBit(value - 1);
        return Integer.numberOfTrailingZeros(highest) + 1;
    }

    private static MTPNDD andRelease(MTPNDD left, MTPNDD right) {
        MTPNDD result = left.and(right);
        left.deref();
        right.deref();
        return result.ref();
    }

    private static MTPNDD orRelease(MTPNDD left, MTPNDD right) {
        MTPNDD result = left.or(right);
        left.deref();
        right.deref();
        return result.ref();
    }

    private static void releaseCache(MTPNDD[][] cache) {
        for (MTPNDD[] row : cache) {
            for (int i = 0; i < row.length; i++) {
                if (row[i] != null) {
                    row[i].deref();
                    row[i] = null;
                }
            }
        }
    }

    private static void releaseArray(MTPNDD[] array) {
        for (int i = 0; i < array.length; i++) {
            if (array[i] != null) {
                array[i].deref();
                array[i] = null;
            }
        }
    }

    public static void main(String[] args) {
        int n = args.length > 0 ? Integer.parseInt(args[0]) : 8;
        boolean useOneHot = args.length > 1 && "onehot".equalsIgnoreCase(args[1]);
        Result result = useOneHot ? solveOneHot(n) : solveBinary(n);
        System.out.printf("n=%d solutions=%d time=%.3fs%n", result.n, result.solutions, result.seconds);
    }

    private static final class Timings {
        private final Map<String, Long> marks = new LinkedHashMap<>();

        void mark(String name) {
            marks.put(name, System.nanoTime());
        }

        double duration(String start, String end) {
            Long s = marks.get(start);
            Long e = marks.get(end);
            if (s == null || e == null) {
                return Double.NaN;
            }
            return (e - s) / 1_000_000_000.0;
        }

        double elapsedSeconds() {
            Long start = marks.get("start");
            Long end = marks.get("satcount");
            if (start == null || end == null) {
                return Double.NaN;
            }
            return (end - start) / 1_000_000_000.0;
        }
    }
}
