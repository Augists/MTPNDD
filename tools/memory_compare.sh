#!/bin/bash
# Memory comparison script for different MTPNDD optimization branches
# Usage: ./memory_compare.sh (run from tools/ directory or project root)

set -e

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_PATH="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_PATH/build"
BENCHMARK_EXEC="$BUILD_DIR/src/sylvan/mtpndd/mtpndd_nqueens_exp"
MEASURE_SCRIPT="$SCRIPT_DIR/measure_memory.sh"

# Test configurations
declare -a TEST_SIZES=("10" "11" "12")
WORKER_COUNT=1

echo "========================================"
echo "MTPNDD Memory Comparison Script"
echo "========================================"
echo ""

# Check if measure_memory.sh exists
if [ ! -f "$MEASURE_SCRIPT" ]; then
    echo "Error: $MEASURE_SCRIPT not found"
    echo "Make sure you run this script from the tools/ directory or project root"
    exit 1
fi

chmod +x "$MEASURE_SCRIPT"

# Get current branch
cd "$REPO_PATH"
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo "Current branch: $CURRENT_BRANCH"
echo ""

# Test current branch
echo "========================================"
echo "Testing current branch: $CURRENT_BRANCH"
echo "========================================"
echo ""

# Build current branch
echo "Building $CURRENT_BRANCH..."
cmake --build "$BUILD_DIR" --target mtpndd_nqueens_exp 2>&1 | tail -5

if [ ! -f "$BENCHMARK_EXEC" ]; then
    echo "Error: Benchmark executable not found after build"
    exit 1
fi

# Create results directory
RESULTS_DIR="/tmp/memory_results_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULTS_DIR"

echo ""
echo "Results will be saved to: $RESULTS_DIR"
echo ""

# Test each size
for N in "${TEST_SIZES[@]}"; do
    echo "----------------------------------------"
    echo "Testing N=$N, Worker=$WORKER_COUNT"
    echo "----------------------------------------"

    OUTPUT_FILE="$RESULTS_DIR/${CURRENT_BRANCH}_N${N}_W${WORKER_COUNT}.txt"

    # Run memory measurement
    "$MEASURE_SCRIPT" proc "$BENCHMARK_EXEC" "$N" "$WORKER_COUNT" 2>&1 | tee "$OUTPUT_FILE"

    # Extract peak memory for summary
    PEAK_MEM=$(grep "PEAK MEMORY USAGE:" "$OUTPUT_FILE" | awk '{print $4}')
    echo "Peak memory: $PEAK_MEM MB"
    echo ""

    sleep 1
done

# Generate summary report
SUMMARY_FILE="$RESULTS_DIR/summary.txt"

echo "========================================" > "$SUMMARY_FILE"
echo "Memory Usage Summary" >> "$SUMMARY_FILE"
echo "Branch: $CURRENT_BRANCH" >> "$SUMMARY_FILE"
echo "========================================" >> "$SUMMARY_FILE"
echo "" >> "$SUMMARY_FILE"

for N in "${TEST_SIZES[@]}"; do
    OUTPUT_FILE="$RESULTS_DIR/${CURRENT_BRANCH}_N${N}_W${WORKER_COUNT}.txt"
    if [ -f "$OUTPUT_FILE" ]; then
        echo "N=$N:" >> "$SUMMARY_FILE"
        grep "PEAK MEMORY USAGE:" "$OUTPUT_FILE" >> "$SUMMARY_FILE"
        grep "Max:" "$OUTPUT_FILE" | grep "RSS" >> "$SUMMARY_FILE"
        echo "" >> "$SUMMARY_FILE"
    fi
done

cat "$SUMMARY_FILE"

echo ""
echo "========================================"
echo "All results saved to: $RESULTS_DIR"
echo "Summary: $SUMMARY_FILE"
echo "========================================"
