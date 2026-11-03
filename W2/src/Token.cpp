 
#include "../include/Token.h"

namespace cmp {

// Initialize the shared token block.
bool initTokenBlock(
    TokenBlock& block,
    const std::uint8_t* src,
    std::uint32_t srcLen
)
{
    if (srcLen > 0 && src == nullptr)
    {
        return false;
    }

    block.reset();
    block.src = src;
    block.srcLen = srcLen;

    return true;
}


// Emit one literal byte and update its frequency.
bool emitLiteral(
    TokenBlock& block,
    std::uint8_t byte
)
{
    if (block.full())
    {
        return false;
    }

    block.addLiteral(byte);
    return true;
}


// Emit one LZ77 match and update its frequencies.
bool emitMatch(
    TokenBlock& block,
    std::uint32_t length,
    std::uint32_t distance
)
{
    if (block.full())
    {
        return false;
    }

    if (length < kMinMatch || length > kMaxMatch)
    {
        return false;
    }

    if (distance == 0 || distance > kMaxDistance)
    {
        return false;
    }

    block.addMatch(length, distance);
    return true;
}


// Emit one project token: (offset, length, nextChar).
bool emitToken(
    TokenBlock& block,
    const Token& token
)
{
    if (token.length == 0)
    {
        // A literal must have offset = 0.
        if (token.offset != 0)
        {
            return false;
        }

        if (block.full())
        {
            return false;
        }
    }
    else
    {
        // Validate the match length.
        if (token.length < kMinMatch ||
            token.length > kMaxMatch)
        {
            return false;
        }

        // Validate the match distance.
        if (token.offset == 0 ||
            token.offset > kMaxDistance)
        {
            return false;
        }

        // A match token needs two packed words:
        // one match word and one literal word.
        if (!block.hasRoomForToken())
        {
            return false;
        }
    }

    block.addToken(token);
    return true;
}


// EOB frequency is initialized by reset().
bool finishTokenBlock(TokenBlock& block)
{
    return block.litlenFreq[kEob] == 1;
}

} // namespace cmp

 