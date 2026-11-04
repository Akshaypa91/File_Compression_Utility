#ifndef TOKEN_H
#define TOKEN_H

#include <cstdint>
#include "symbols.hpp"

using namespace std;

namespace cmp {

// Initialize a token block for the given source data.
bool initTokenBlock(
    TokenBlock& block,
    const uint8_t* src,
    uint32_t srcLen
);

// Emit one literal byte.
bool emitLiteral(
    TokenBlock& block,
    uint8_t byte
);

// Emit one LZ77 match.
bool emitMatch(
    TokenBlock& block,
    uint32_t length,
    uint32_t distance
);

// Emit one project token: (offset, length, nextChar).
bool emitToken(
    TokenBlock& block,
    const Token& token
);

// Finish the block.
bool finishTokenBlock(TokenBlock& block);

} // namespace cmp

#endif