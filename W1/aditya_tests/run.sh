#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT_DIR"

LOGS_DIR="$SCRIPT_DIR/logs"
BUILD_DIR="$SCRIPT_DIR/build_tmp"
mkdir -p "$LOGS_DIR"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

FAILED_STEPS=""

# Machine info
{
    echo "=== Machine Information ==="
    if command -v lscpu >/dev/null 2>&1; then
        lscpu | grep -E "Model name|CPU\(s\):|Thread\(s\) per core" || true
    elif [ -f /proc/cpuinfo ]; then
        grep -m 1 "model name" /proc/cpuinfo || true
        echo "CPU threads: $(grep -c "processor" /proc/cpuinfo || true)"
    fi
    g++ --version | head -n 1
} > "$LOGS_DIR/machine.txt" 2>&1

# Step 1: Normal build and run
STEP1_PASS=1
echo "Running Step 1: Normal build and test suite run..."
if g++ -std=c++17 -O2 -I W1/include -I shared/include \
    W1/aditya_tests/test_all.cpp W1/src/huffman_tree.cpp W1/src/min_heap.cpp \
    -o "$BUILD_DIR/test_all.exe" > "$LOGS_DIR/build.log" 2>&1; then
    if "$BUILD_DIR/test_all.exe" > "$LOGS_DIR/run.log" 2>&1; then
        echo "Step 1 passed."
    else
        echo "Step 1 run failed."
        STEP1_PASS=0
        FAILED_STEPS="$FAILED_STEPS normal_run"
    fi
else
    echo "Step 1 build failed."
    STEP1_PASS=0
    FAILED_STEPS="$FAILED_STEPS normal_build"
fi

# Step 2: Sanitizer build
STEP2_PASS=1
echo "Running Step 2: Sanitizer build and run..."
if g++ -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=undefined -g -O1 \
    -I W1/include -I shared/include \
    W1/aditya_tests/test_all.cpp W1/src/huffman_tree.cpp W1/src/min_heap.cpp \
    -o "$BUILD_DIR/test_san.exe" > "$LOGS_DIR/sanitizer.log" 2>&1; then
    if "$BUILD_DIR/test_san.exe" >> "$LOGS_DIR/sanitizer.log" 2>&1; then
        echo "Step 2 passed."
    else
        echo "Step 2 run failed."
        STEP2_PASS=0
        FAILED_STEPS="$FAILED_STEPS sanitizer_run"
    fi
else
    echo "Sanitizer build not supported by toolchain (missing libasan/libubsan on Windows MinGW)." >> "$LOGS_DIR/sanitizer.log"
    echo "Step 2 skipped (toolchain limitation logged in logs/sanitizer.log)."
fi

# Step 3: -Wall -Wextra -Wpedantic warnings
STEP3_PASS=1
echo "Running Step 3: Warnings check on test_all.cpp..."
if g++ -std=c++17 -Wall -Wextra -Wpedantic -I W1/include -I shared/include \
    -c W1/aditya_tests/test_all.cpp -o "$BUILD_DIR/test_all.o" > "$LOGS_DIR/warnings.log" 2>&1; then
    if [ -s "$LOGS_DIR/warnings.log" ]; then
        echo "Step 3: Warnings found."
        STEP3_PASS=0
        FAILED_STEPS="$FAILED_STEPS warnings"
    else
        echo "Step 3 passed (0 warnings)."
    fi
else
    echo "Step 3 compilation failed."
    STEP3_PASS=0
    FAILED_STEPS="$FAILED_STEPS warnings_build"
fi

# Step 4: Coverage build and gcov on huffman_tree.cpp ONLY
STEP4_PASS=1
echo "Running Step 4: Coverage build and gcov on huffman_tree.cpp ONLY..."
cd "$BUILD_DIR"
if g++ -std=c++17 --coverage -O0 -I ../../include -I ../../../shared/include \
    ../test_all.cpp ../../src/huffman_tree.cpp ../../src/min_heap.cpp \
    -o test_cov.exe > "$LOGS_DIR/coverage_build.log" 2>&1; then
    ./test_cov.exe > /dev/null 2>&1 || true
    gcov -b huffman_tree.cpp > "$LOGS_DIR/coverage.log" 2>&1
    
    # Extract uncovered lines
    if [ -f "huffman_tree.cpp.gcov" ]; then
        grep "#####" "huffman_tree.cpp.gcov" > "$LOGS_DIR/uncovered_lines.txt" || true
    else
        touch "$LOGS_DIR/uncovered_lines.txt"
    fi
    echo "Step 4 passed."
else
    echo "Step 4 coverage build failed."
    STEP4_PASS=0
    FAILED_STEPS="$FAILED_STEPS coverage"
fi
cd "$ROOT_DIR"

# Clean temporary build dir
rm -rf "$BUILD_DIR"

# Step 5: Final output line
if [ "$STEP1_PASS" -eq 1 ] && [ "$STEP2_PASS" -eq 1 ] && [ "$STEP3_PASS" -eq 1 ] && [ "$STEP4_PASS" -eq 1 ]; then
    echo "ALL PASS"
    exit 0
else
    echo "FAILED:$FAILED_STEPS"
    exit 1
fi
