#!/bin/bash
set -e
rm -rf build_cov_clean
mkdir -p build_cov_clean

FLAGS="-std=c++17 -O0 -g --coverage -I W1/include -I shared/include -I W1/tests"

for f in W1/src/*.cpp; do
  g++ $FLAGS -c "$f" -o build_cov_clean/$(basename "$f" .cpp).o
done

for t in test_bit_io test_tree test_huffman test_gaps test_oat test_min_heap test_types test_symbols; do
  g++ $FLAGS W1/tests/$t.cpp build_cov_clean/*.o -o build_cov_clean/$t
done

for t in test_bit_io test_tree test_huffman test_gaps test_oat test_min_heap test_types test_symbols; do
  ./build_cov_clean/$t
done
echo "CLEAN BUILD OK"
