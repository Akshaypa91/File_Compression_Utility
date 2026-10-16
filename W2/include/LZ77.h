#ifndef LZ77_H
#define LZ77_H

#include <cstdint>
#include "symbols.hpp"
#include "HashChain.h"

// LZ77 configuration
constexpr int LZ77_HASH_SIZE = 32768;
constexpr int LZ77_WINDOW_SIZE = 32768;
constexpr int LZ77_MAX_HASH_DEPTH = 256;
constexpr int LZ77_LOOKAHEAD_SIZE = 258;

namespace cmp {

// Compress the source data using LZ77.
// Store generated tokens and symbol frequencies in TokenBlock.
bool compressLZ77(
    TokenBlock& block,
    const std::uint8_t* src,
    std::uint32_t srcLen
);

} // namespace cmp

#endif