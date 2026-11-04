#!/bin/bash
set -e

mkdir -p build_cov

FLAGS="-std=c++17 -O0 -g --coverage -fsanitize=address,undefined -I W1/include -I shared/include -I W1/tests"

# compile each W1 source file
for f in W1/src/*.cpp; do
  g++ $FLAGS -c "$f" -o build_cov/$(basename "$f" .cpp).o
done

# build the three test programs
for t in test_bit_io test_tree test_huffman test_gaps test_oat test_min_heap test_types test_symbols; do
  g++ $FLAGS W1/tests/$t.cpp build_cov/*.o -o build_cov/$t
done

echo "BUILD OK"
