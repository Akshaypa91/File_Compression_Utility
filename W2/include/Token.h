#ifndef TOKEN_H
#define TOKEN_H

#include <cstdint>
#include "symbols.hpp"

namespace cmp {

// Initialize a token block for the given source data.
bool initTokenBlock(
    TokenBlock& block,
    const std::uint8_t* src,
    std::uint32_t srcLen
);

// Emit one literal byte.
bool emitLiteral(
    TokenBlock& block,
    std::uint8_t byte
);

// Emit one LZ77 match.
bool emitMatch(
    TokenBlock& block,
    std::uint32_t length,
    std::uint32_t distance
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