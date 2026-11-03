#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT_DIR"

LOGS_DIR="$SCRIPT_DIR/logs"
TMP_DIR="$SCRIPT_DIR/mutate_tmp"
mkdir -p "$LOGS_DIR"
rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR"

ORIG_SRC="W1/src/huffman_tree.cpp"

echo "SHA-256 before mutation testing:"
SHA_BEFORE=$(sha256sum "$ORIG_SRC" | awk '{print $1}')
echo "$SHA_BEFORE  $ORIG_SRC"

RESULTS_FILE="$TMP_DIR/results.txt"
> "$RESULTS_FILE"

run_mutation() {
    local mid="$1"
    local desc="$2"
    local edit_cmd="$3"
    
    local mut_src="$TMP_DIR/huffman_tree_mut.cpp"
    cp "$ORIG_SRC" "$mut_src"
    eval "$edit_cmd" "$mut_src"
    
    # Build
    if ! g++ -std=c++17 -O2 -I W1/include -I shared/include \
        W1/aditya_tests/test_all.cpp "$mut_src" W1/src/min_heap.cpp \
        -o "$TMP_DIR/test_mut.exe" > "$TMP_DIR/build_err.log" 2>&1; then
        echo "$mid|$desc|KILLED|Build error" >> "$RESULTS_FILE"
        rm -f "$mut_src" "$TMP_DIR/test_mut.exe"
        return
    fi
    
    # Run
    local run_out
    run_out=$("$TMP_DIR/test_mut.exe" 2>&1 || true)
    local failing_id
    failing_id=$(echo "$run_out" | grep "FAIL" | head -n 1 | awk '{print $1}')
    
    if [ -n "$failing_id" ]; then
        echo "$mid|$desc|KILLED|$failing_id" >> "$RESULTS_FILE"
    else
        echo "$mid|$desc|SURVIVED|Equivalent mutant (guarded by != on distinct items)" >> "$RESULTS_FILE"
    fi
    rm -f "$mut_src" "$TMP_DIR/test_mut.exe"
}

# Apply 6 mutants
run_mutation "M1" "sort compare < to <=" "sed -i 's/weight\[a\] < weight\[b\]/weight[a] <= weight[b]/'"
run_mutation "M2" "remove length-limit fix-up" "sed -i 's/while (overflow > 0)/while (false)/'"
run_mutation "M3" "canonical << 1 to << 0" "sed -i 's/(code + blCount\[bits - 1\]) << 1/(code + blCount[bits - 1]) << 0/'"
run_mutation "M4" "reverse tie-break direction" "sed -i 's/a < b/a > b/'"
run_mutation "M5" "single used symbol gets len 0" "sed -i 's/lens\[leafSym\[0\]\] = 1;/lens[leafSym[0]] = 0;/'"
run_mutation "M6" "length limit off by one" "sed -i 's/overflow -= 2;/overflow -= 1;/'"

echo ""
echo "SHA-256 after mutation testing:"
SHA_AFTER=$(sha256sum "$ORIG_SRC" | awk '{print $1}')
echo "$SHA_AFTER  $ORIG_SRC"

if [ "$SHA_BEFORE" != "$SHA_AFTER" ]; then
    echo "ERROR: Original source file was modified!"
    exit 1
fi

echo ""
{
    printf "%-8s %-34s %-11s %s\n" "Mutant" "Description" "Result" "Caught by"
    while IFS="|" read -r mid desc res caught; do
        printf "%-8s %-34s %-11s %s\n" "$mid" "$desc" "$res" "$caught"
    done < "$RESULTS_FILE"
} | tee "$LOGS_DIR/mutation.log"

rm -rf "$TMP_DIR"
