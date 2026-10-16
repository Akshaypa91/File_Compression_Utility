// huffman_tree.h - Huffman construction: lengths from frequencies, length limiting,
// canonical codes, prefix-free verification, and a reference trie decoder.
//
// Flow:  frequencies --(min-heap greedy merge)--> code lengths --(canonical rule)--> codes.
// Only the lengths are stored in the compressed file; the decoder rebuilds identical codes.
#pragma once

#include <cstdint>
#include <vector>

#include "bit_io.h"

namespace w1 {

// Builds Huffman code lengths for `n` symbols (n <= 286) with the min-heap greedy algorithm.
//   freq[i] == 0  -> lens[i] = 0 (symbol unused)
//   exactly one used symbol -> its length is 1 (never an empty code)
//   lengths are limited to `maxLen` (<= 15) using the zlib-style overflow fix-up; this only
//   triggers on very skewed distributions and keeps the Kraft sum <= 1.
// Ties are broken by (weight, node id), so the result is deterministic everywhere.
void buildLengths(const std::uint32_t* freq, unsigned n, unsigned maxLen, std::uint8_t* lens);

// Canonical code assignment: sort by (length, symbol), start at 0, count upward, shift left
// when the length grows. Codes are right-aligned in `codes[i]`; unused symbols get 0.
void assignCanonical(const std::uint8_t* lens, unsigned n, std::uint16_t* codes);

// Kraft sum scaled by 2^15:  sum over used symbols of 2^(15 - len).
//   == 1<<15 : complete code.   < 1<<15 : incomplete (valid).   > 1<<15 : over-subscribed (invalid).
std::uint64_t kraftSum(const std::uint8_t* lens, unsigned n);

// Explicit prefix-free check: Kraft sum <= 1 AND no code is a prefix of (or equal to) another.
bool verifyPrefixFree(const std::uint8_t* lens, const std::uint16_t* codes, unsigned n);

// Reference decoder: a binary trie of the canonical codes, decoded by walking it bit by bit.
// This is the textbook "decode by walking the Huffman trie". The production decoder uses
// table lookup instead, and the tests check that both give identical results.
struct CodeTrie {
    std::vector<int> child0, child1;   // -1 = none
    std::vector<int> symbol;           // -1 = internal node
};
bool buildTrie(const std::uint8_t* lens, const std::uint16_t* codes, unsigned n, CodeTrie& trie);
// Returns the decoded symbol, or -1 if the bits run out or hit an unused branch.
int trieDecode(const CodeTrie& trie, BitReader& reader);

}  // namespace w1