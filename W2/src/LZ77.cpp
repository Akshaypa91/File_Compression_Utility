#include "../include/LZ77.h"
#include "../include/HashChain.h"
#include "../include/Token.h"

#include <cstdint>

namespace
{
    // Hash three consecutive bytes to find possible matches.
    std::uint32_t hashThreeBytes(
        const std::uint8_t* input,
        int position
    )
    {
        std::uint32_t a = input[position];
        std::uint32_t b = input[position + 1];
        std::uint32_t c = input[position + 2];

        std::uint32_t hash =
            a * 257u +
            b * 263u +
            c * 269u;

        return hash & (LZ77_HASH_SIZE - 1);
    }


    // Insert a valid position into the hash chain.
    void insertPosition(
        HashChain* chain,
        const std::uint8_t* input,
        int position,
        int inputSize
    )
    {
        if (position + 2 >= inputSize)
        {
            return;
        }

        std::uint32_t hash =
            hashThreeBytes(input, position);

        insertHashPosition(
            chain,
            static_cast<int>(hash),
            position
        );
    }


    int smaller(int a, int b)
    {
        return (a < b) ? a : b;
    }
}


namespace cmp
{

bool compressLZ77(
    TokenBlock& block,
    const std::uint8_t* src,
    std::uint32_t srcLen
)
{
    if (srcLen > 0 && src == nullptr)
    {
        return false;
    }

    // Initialize the shared block.
    if (!initTokenBlock(block, src, srcLen))
    {
        return false;
    }

    if (srcLen == 0)
    {
        return finishTokenBlock(block);
    }

    // The current hash-chain API uses int positions.
    if (srcLen > static_cast<std::uint32_t>(INT32_MAX))
    {
        return false;
    }

    const int inputSize = static_cast<int>(srcLen);

    HashChain chain{};

    if (!initHashChain(
            &chain,
            inputSize,
            LZ77_HASH_SIZE))
    {
        return false;
    }

    int current = 0;

    while (current < inputSize)
    {
        int bestLength = 0;
        int bestPosition = -1;

        // A three-byte hash requires at least three input bytes.
        if (current + 2 < inputSize)
        {
            std::uint32_t hash =
                hashThreeBytes(src, current);

            int candidate =
                getHashHead(&chain, static_cast<int>(hash));

            int windowStart =
                current - LZ77_WINDOW_SIZE;

            if (windowStart < 0)
            {
                windowStart = 0;
            }

            int depth = 0;

            // Search previous positions with the same hash.
            while (
                candidate >= windowStart &&
                candidate >= 0 &&
                depth < LZ77_MAX_HASH_DEPTH
            )
            {
                int remaining = inputSize - current;

                // Preserve one byte for nextChar.
                int maximumLength = remaining - 1;

                maximumLength = smaller(
                    maximumLength,
                    LZ77_LOOKAHEAD_SIZE
                );

                int length = 0;

                // Compare the candidate with the current sequence.
                while (
                    length < maximumLength &&
                    src[candidate + length] ==
                        src[current + length]
                )
                {
                    ++length;
                }

                if (length > bestLength)
                {
                    bestLength = length;
                    bestPosition = candidate;

                    if (length == maximumLength)
                    {
                        break;
                    }
                }

                candidate =
                    getPreviousPosition(&chain, candidate);

                ++depth;
            }
        }

        if (bestLength >= static_cast<int>(kMinMatch))
        {
            // Emit a match followed by its next literal.
            Token token{};

            token.offset = static_cast<std::uint32_t>(
                current - bestPosition
            );

            token.length = static_cast<std::uint32_t>(
                bestLength
            );

            token.nextChar = src[current + bestLength];

            if (!emitToken(block, token))
            {
                freeHashChain(&chain);
                return false;
            }

            // Insert the consumed positions into the hash chain.
            int advance = bestLength + 1;

            for (
                int position = current;
                position < current + advance &&
                    position < inputSize;
                ++position
            )
            {
                insertPosition(
                    &chain,
                    src,
                    position,
                    inputSize
                );
            }

            current += advance;
        }
        else
        {
            // No match: emit a literal token.
            Token token{};

            token.offset = 0;
            token.length = 0;
            token.nextChar = src[current];

            if (!emitToken(block, token))
            {
                freeHashChain(&chain);
                return false;
            }

            insertPosition(
                &chain,
                src,
                current,
                inputSize
            );

            ++current;
        }
    }

    freeHashChain(&chain);

    return finishTokenBlock(block);
}

} // namespace cmp