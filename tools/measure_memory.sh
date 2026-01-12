#!/bin/bash
# Memory measurement using valgrind massif or direct /proc monitoring
# Usage: ./measure_memory_v2.sh <method> <executable> <args...>

METHOD="$1"
shift

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <method> <executable> [args...]"
    echo "Methods:"
    echo "  massif    - Use valgrind massif (accurate but slow)"
    echo "  proc      - Use /proc monitoring (fast but may miss short-lived peaks)"
    echo "Example: $0 massif ./build/src/sylvan/mtpndd/mtpndd_nqueens_exp 12 1"
    exit 1
fi

EXEC_PATH="$1"
shift
EXEC_ARGS="$@"

if [ ! -f "$EXEC_PATH" ]; then
    echo "Error: Executable not found: $EXEC_PATH"
    exit 1
fi

echo "========================================"
echo "MTPNDD Memory Measurement"
echo "========================================"
echo "Method: $METHOD"
echo "Command: $EXEC_PATH $EXEC_ARGS"
echo "Started at: $(date)"
echo ""

if [ "$METHOD" = "massif" ]; then
    # Use valgrind massif for accurate measurement
    MASSIF_OUT="/tmp/massif.out.$$"

    echo "Running with valgrind massif (this will be slow)..."
    valgrind --tool=massif --massif-out-file="$MASSIF_OUT" \
        "$EXEC_PATH" $EXEC_ARGS 2>&1 | grep -E "solutions=|baseline_set_sizes"

    if [ ! -f "$MASSIF_OUT" ]; then
        echo "Error: Massif output not found"
        exit 1
    fi

    echo ""
    echo "========================================"
    echo "Memory Statistics (Massif)"
    echo "========================================"

    # Parse massif output
    PEAK_BYTES=$(grep "mem_heap_B=" "$MASSIF_OUT" | sed 's/mem_heap_B=//' | sort -n | tail -1)
    if [ -n "$PEAK_BYTES" ]; then
        PEAK_MB=$(awk "BEGIN {printf \"%.2f\", $PEAK_BYTES / 1048576}")
        echo "Peak heap memory: $PEAK_BYTES bytes ($PEAK_MB MB)"
    fi

    echo ""
    echo "Detailed massif report:"
    ms_print "$MASSIF_OUT" | head -50

    echo ""
    echo "Full massif output saved to: $MASSIF_OUT"

elif [ "$METHOD" = "proc" ]; then
    # Monitor /proc/PID/status during execution
    # Write output to file to avoid interfering with memory measurement
    STDOUT_FILE="/tmp/proc_stdout_$$.txt"
    STATS_FILE="/tmp/proc_stats_$$.txt"

    # Run process in background
    "$EXEC_PATH" $EXEC_ARGS > "$STDOUT_FILE" 2>&1 &
    PID=$!

    echo "Process PID: $PID"
    echo "Monitoring memory usage..."

    # Monitor loop
    PEAK_RSS=0
    PEAK_VMS=0
    SAMPLE_COUNT=0

    while kill -0 "$PID" 2>/dev/null; do
        if [ -f "/proc/$PID/status" ]; then
            RSS_KB=$(grep "^VmRSS:" /proc/$PID/status 2>/dev/null | awk '{print $2}')
            VMS_KB=$(grep "^VmSize:" /proc/$PID/status 2>/dev/null | awk '{print $2}')

            if [ -n "$RSS_KB" ]; then
                if [ $RSS_KB -gt $PEAK_RSS ]; then
                    PEAK_RSS=$RSS_KB
                fi
                ((SAMPLE_COUNT++))
            fi

            if [ -n "$VMS_KB" ] && [ $VMS_KB -gt $PEAK_VMS ]; then
                PEAK_VMS=$VMS_KB
            fi
        fi
        sleep 0.02  # 20ms sampling interval
    done

    # Wait for process
    wait $PID
    EXIT_CODE=$?

    echo ""
    echo "========================================"
    echo "Memory Statistics (/proc monitoring)"
    echo "========================================"
    echo "Samples collected: $SAMPLE_COUNT"

    if [ $PEAK_RSS -gt 0 ]; then
        PEAK_RSS_MB=$(awk "BEGIN {printf \"%.2f\", $PEAK_RSS / 1024}")
        echo "Peak RSS (Physical Memory): $PEAK_RSS KB ($PEAK_RSS_MB MB)"
    else
        echo "Warning: No RSS samples collected (process too fast?)"
    fi

    if [ $PEAK_VMS -gt 0 ]; then
        PEAK_VMS_MB=$(awk "BEGIN {printf \"%.2f\", $PEAK_VMS / 1024}")
        echo "Peak VMS (Virtual Memory):  $PEAK_VMS KB ($PEAK_VMS_MB MB)"
    fi

    echo ""
    echo "Exit code: $EXIT_CODE"
    echo ""
    echo "Program output:"
    grep -E "solutions=|baseline_set_sizes" "$STDOUT_FILE"

    if [ $PEAK_RSS -gt 0 ]; then
        echo ""
        echo "=========================================="
        echo "PEAK MEMORY USAGE: $PEAK_RSS_MB MB"
        echo "=========================================="
    fi

else
    echo "Error: Unknown method '$METHOD'"
    echo "Use 'massif' or 'proc'"
    exit 1
fi
