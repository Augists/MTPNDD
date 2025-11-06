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

    public static Result solve(int n) {
        if (n <= 0) {
            throw new IllegalArgumentException("n must be positive");
        }
        MTPNDDConfig config = MTPNDDConfig.builder()
                .workers(0)
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
                fieldIds[i] = MTPNDDEngine.declareField(n);
            }

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
                MTPNDD clause = MTPNDD.terminalFalse();
                for (int col = 0; col < n; col++) {
                    clause = clause.or(vars[row][col]);
                }
                rowRequirements[row] = clause;
            }

            MTPNDD[][] cellConstraints = new MTPNDD[n][n];
            for (int row = 0; row < n; row++) {
                for (int col = 0; col < n; col++) {
                    cellConstraints[row][col] = buildCellConstraints(row, col, n, vars, notVars);
                }
            }

            MTPNDD formula = MTPNDD.terminalTrue();
            for (MTPNDD requirement : rowRequirements) {
                formula = formula.and(requirement);
            }
            for (int row = 0; row < n; row++) {
                for (int col = 0; col < n; col++) {
                    formula = formula.and(cellConstraints[row][col]);
                }
            }

            double solutions = formula.satCount();

            double elapsedSeconds = (System.nanoTime() - start) / 1_000_000_000.0;
            return new Result(n, elapsedSeconds, Math.round(solutions));
        } finally {
            MTPNDDEngine.shutdown();
        }
    }

    private static MTPNDD buildCellConstraints(int row, int col, int n,
                                               MTPNDD[][] vars,
                                               MTPNDD[][] notVars) {
        MTPNDD guard = vars[row][col];
        MTPNDD constraints = MTPNDD.terminalTrue();

        for (int otherCol = 0; otherCol < n; otherCol++) {
            if (otherCol == col) {
                continue;
            }
            constraints = constraints.and(implies(guard, notVars[row][otherCol]));
        }

        for (int otherRow = 0; otherRow < n; otherRow++) {
            if (otherRow == row) {
                continue;
            }
            constraints = constraints.and(implies(guard, notVars[otherRow][col]));
        }

        for (int otherRow = 0; otherRow < n; otherRow++) {
            if (otherRow == row) {
                continue;
            }
            int diagRight = col + (otherRow - row);
            if (diagRight >= 0 && diagRight < n) {
                constraints = constraints.and(implies(guard, notVars[otherRow][diagRight]));
            }
            int diagLeft = col - (otherRow - row);
            if (diagLeft >= 0 && diagLeft < n) {
                constraints = constraints.and(implies(guard, notVars[otherRow][diagLeft]));
            }
        }

        return constraints;
    }

    private static MTPNDD implies(MTPNDD premise, MTPNDD consequence) {
        return premise.not().or(consequence);
    }

    public static void main(String[] args) {
        int n = args.length > 0 ? Integer.parseInt(args[0]) : 8;
        Result result = solve(n);
        System.out.printf("n=%d solutions=%d time=%.3fs%n", result.n, result.solutions, result.seconds);
    }
}
