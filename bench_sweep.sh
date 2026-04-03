#!/usr/bin/env bash
# bench_sweep.sh — Build and benchmark nqueens N=4..13, workers=1..16
# Usage: ./bench_sweep.sh [--skip-build] [--n-min=4] [--n-max=13] [--w-min=1] [--w-max=16] [--parallel]
#
#   --parallel   Run mtpndd_nqueens_parallel_benchmark instead of the serial one.
#                Use this to compare outer-operation parallelism vs serial build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYLVAN_DIR="$SCRIPT_DIR/sylvan"
SERIAL_BIN="$SYLVAN_DIR/build/src/sylvan/mtpndd/mtpndd_nqueens_benchmark"
PARALLEL_BIN="$SYLVAN_DIR/build/src/sylvan/mtpndd/mtpndd_nqueens_parallel_benchmark"

N_MIN=14
N_MAX=14
W_MIN=1
W_MAX=16
SKIP_BUILD=0
USE_PARALLEL=0

for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        --parallel)   USE_PARALLEL=1 ;;
        --n-max=*)    N_MAX="${arg#*=}" ;;
        --w-max=*)    W_MAX="${arg#*=}" ;;
        --n-min=*)    N_MIN="${arg#*=}" ;;
        --w-min=*)    W_MIN="${arg#*=}" ;;
    esac
done

if [[ "$USE_PARALLEL" -eq 1 ]]; then
    BENCH_BIN="$PARALLEL_BIN"
    BENCH_LABEL="parallel"
else
    BENCH_BIN="$SERIAL_BIN"
    BENCH_LABEL="serial"
fi

# ── Build ──────────────────────────────────────────────────────────────────────
if [[ "$SKIP_BUILD" -eq 0 ]]; then
    echo "[bench_sweep] Removing old build dir..."
    rm -rf "$SYLVAN_DIR/build"
    echo "[bench_sweep] Configuring sylvan (LOG_LEVEL=0)..."
    (cd "$SYLVAN_DIR" && cmake -B build -DMTPNDD_LOG_LEVEL=0)
    echo "[bench_sweep] Building..."
    (cd "$SYLVAN_DIR" && cmake --build build --parallel)
    echo "[bench_sweep] Build done."
fi

if [[ ! -x "$BENCH_BIN" ]]; then
    echo "ERROR: benchmark binary not found at $BENCH_BIN" >&2
    exit 1
fi

# ── Collect results ────────────────────────────────────────────────────────────
RESULT_FILE="$SCRIPT_DIR/bench_results_${BENCH_LABEL}_$(date +%Y%m%d_%H%M%S).tsv"
printf "N\tworkers\telapsed_s\tsolutions\n" > "$RESULT_FILE"

printf "%-4s  %-7s  %-12s  %-12s\n" "N" "workers" "elapsed(s)" "solutions"
printf "%s\n" "----  -------  ------------  ------------"

for n in $(seq "$N_MIN" "$N_MAX"); do
    for w in $(seq "$W_MIN" "$W_MAX"); do
        raw=$("$BENCH_BIN" "$n" "$w" 2>/dev/null || true)
        # output format: \t<elapsed>\t<solutions>  (leading tab, skip stats lines)
        result_line=$(printf '%s\n' "$raw" | grep -v '^\.\.' | head -1)
        elapsed=$(printf '%s\n' "$result_line" | awk '{print $1}')
        solutions=$(printf '%s\n' "$result_line" | awk '{print $2}')

        if [[ -z "$elapsed" ]]; then
            elapsed="ERR"
            solutions="ERR"
        fi

        printf "%d\t%d\t%s\t%s\n" "$n" "$w" "$elapsed" "$solutions" >> "$RESULT_FILE"
        printf "%-4d  %-7d  %-12s  %-12s\n" "$n" "$w" "$elapsed" "$solutions"
    done
done

echo ""
echo "[bench_sweep] Results saved to: $RESULT_FILE"
