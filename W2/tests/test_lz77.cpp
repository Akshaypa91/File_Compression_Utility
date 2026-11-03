#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
#include <iterator>
#include <limits>

#include "../include/LZ77.h"
#include "../include/Token.h"
#include "../shared/include/symbols.hpp"

// --------------------------------------------------
// DISPLAY LZ77 TOKEN TRIPLETS AND FREQUENCY TABLE
// --------------------------------------------------

void displayTokensAndFrequencies(
    const std::uint8_t* input,
    std::uint32_t inputSize,
    const char* filename)
{
    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, input, inputSize))
    {
        std::printf("ERROR: Token generation failed.\n");
        return;
    }

    // --------------------------------------------------
    // LZ77 TOKEN SEQUENCE
    // --------------------------------------------------

    std::printf("\n");
    std::printf("============================================================\n");
    std::printf("                  LZ77 TOKEN SEQUENCE\n");
    std::printf("============================================================\n");

    std::printf("\nInput file: %s\n", filename);
    std::printf("Input size: %u bytes\n\n",
                static_cast<unsigned>(inputSize));

    std::printf("Generated Tokens:\n");

    std::uint32_t tokenCount = 0;

    for (std::uint32_t i = 0; i < block.n; ++i)
    {
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
        std::uint8_t nextChar;

        if (cmp::isMatch(block.tok[i]))
        {
            offset = cmp::distOf(block.tok[i]);
            length = cmp::lenOf(block.tok[i]);

            // The next compact word stores nextChar.
            ++i;

            if (i >= block.n || cmp::isMatch(block.tok[i]))
            {
                std::printf("ERROR: Invalid compact token sequence.\n");
                return;
            }

            nextChar = cmp::litOf(block.tok[i]);
        }
        else
        {
            nextChar = cmp::litOf(block.tok[i]);
        }

        // Display the original W2 triplet representation.
        std::printf("{%u, %u, '%c'}\n",
                    static_cast<unsigned>(offset),
                    static_cast<unsigned>(length),
                    static_cast<char>(nextChar));

        ++tokenCount;
    }

    std::printf("\nTotal Tokens: %u\n",
                static_cast<unsigned>(tokenCount));


    // --------------------------------------------------
    // FREQUENCY TABLE
    // --------------------------------------------------

    std::printf("\n");
    std::printf("============================================================\n");
    std::printf("                     FREQUENCY TABLE\n");
    std::printf("============================================================\n");

    // Literal/Length frequencies
    std::printf("\nLITERAL / LENGTH SYMBOL FREQUENCIES\n");
    std::printf("------------------------------------------------------------\n");

    for (unsigned i = 0; i < cmp::kNumLitLen; ++i)
    {
        if (block.litlenFreq[i] == 0)
            continue;

        if (i < 256)
        {
            std::printf("'%c' : %u\n",
                        static_cast<char>(i),
                        static_cast<unsigned>(block.litlenFreq[i]));
        }
        else if (i == cmp::kEob)
        {
            std::printf("EOB : %u\n",
                        static_cast<unsigned>(block.litlenFreq[i]));
        }
        else
        {
            std::printf("Length symbol %u : %u\n",
                        i,
                        static_cast<unsigned>(block.litlenFreq[i]));
        }
    }

    // Distance frequencies
    std::printf("\nDISTANCE SYMBOL FREQUENCIES\n");
    std::printf("------------------------------------------------------------\n");

    for (unsigned i = 0; i < cmp::kNumDist; ++i)
    {
        if (block.distFreq[i] != 0)
        {
            std::printf("Distance symbol %u : %u\n",
                        i,
                        static_cast<unsigned>(block.distFreq[i]));
        }
    }

    std::printf("------------------------------------------------------------\n");

    std::printf("Total Extra Bits: %llu\n",
                static_cast<unsigned long long>(block.extraBitsTotal));
}


// --------------------------------------------------
// TEST CASE 1: BASIC COMPRESSION
// --------------------------------------------------

int testBasicCompression()
{
    const std::uint8_t input[] = "ABABABABABABABCD";

    const std::uint32_t inputSize =
        static_cast<std::uint32_t>(sizeof(input) - 1);

    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, input, inputSize))
    {
        std::printf("FAIL: Basic compression\n");
        return 1;
    }

    if (block.n == 0 ||
        block.src != input ||
        block.srcLen != inputSize ||
        block.litlenFreq[cmp::kEob] != 1)
    {
        std::printf("FAIL: Basic compression validation\n");
        return 1;
    }

    bool foundMatch = false;

    for (std::uint32_t i = 0; i < block.n; ++i)
    {
        if (cmp::isMatch(block.tok[i]))
        {
            foundMatch = true;
            break;
        }
    }

    if (!foundMatch)
    {
        std::printf("FAIL: Match generation\n");
        return 1;
    }

    std::printf("PASS: Basic compression\n");
    return 0;
}


// --------------------------------------------------
// TEST CASE 2: EMPTY INPUT
// --------------------------------------------------

int testEmptyInput()
{
    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, nullptr, 0))
    {
        std::printf("FAIL: Empty input\n");
        return 1;
    }

    if (block.n != 0 ||
        block.litlenFreq[cmp::kEob] != 1)
    {
        std::printf("FAIL: Empty input validation\n");
        return 1;
    }

    std::printf("PASS: Empty input\n");
    return 0;
}


// --------------------------------------------------
// TEST CASE 3: SINGLE BYTE INPUT
// --------------------------------------------------

int testSingleByteInput()
{
    const std::uint8_t input[] = {'A'};

    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, input, 1))
    {
        std::printf("FAIL: Single-byte input\n");
        return 1;
    }

    if (block.n != 1 ||
        cmp::isMatch(block.tok[0]) ||
        cmp::litOf(block.tok[0]) != 'A')
    {
        std::printf("FAIL: Single-byte input validation\n");
        return 1;
    }

    std::printf("PASS: Single-byte input\n");
    return 0;
}
// --------------------------------------------------
// TEST CASE 4: MINIMUM MATCH LENGTH
// --------------------------------------------------

int testMinimumMatchLength()
{
    const std::uint8_t input[] = "ABCABCX";
    const std::uint32_t inputSize = sizeof(input) - 1;

    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, input, inputSize))
    {
        std::printf("FAIL: Minimum match length\n");
        return 1;
    }

    // First three words should be literals A, B, C.
    // Fourth word should be a match.
    if (block.n != 5 ||
        !cmp::isMatch(block.tok[3]))
    {
        std::printf("FAIL: Minimum match was not generated\n");
        return 1;
    }

    if (cmp::lenOf(block.tok[3]) != cmp::kMinMatch ||
        cmp::distOf(block.tok[3]) != 3 ||
        cmp::isMatch(block.tok[4]) ||
        cmp::litOf(block.tok[4]) != 'X')
    {
        std::printf("FAIL: Incorrect minimum match token\n");
        return 1;
    }

    std::printf("PASS: Minimum match length\n");
    return 0;
}


// --------------------------------------------------
// TEST CASE 5: NO REPEATED CHARACTERS
// --------------------------------------------------

int testNoRepeatedCharacters()
{
    const std::uint8_t input[] = "ABCDEFGH";
    const std::uint32_t inputSize = sizeof(input) - 1;

    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, input, inputSize))
    {
        std::printf("FAIL: No repeated characters\n");
        return 1;
    }

    if (block.n != inputSize)
    {
        std::printf("FAIL: Unexpected compact word count\n");
        return 1;
    }

    for (std::uint32_t i = 0; i < block.n; ++i)
    {
        if (cmp::isMatch(block.tok[i]) ||
            cmp::litOf(block.tok[i]) != input[i])
        {
            std::printf("FAIL: Unexpected match in unique input\n");
            return 1;
        }
    }

    for (unsigned i = 0; i < cmp::kNumDist; ++i)
    {
        if (block.distFreq[i] != 0)
        {
            std::printf("FAIL: Unexpected distance frequency\n");
            return 1;
        }
    }

    std::printf("PASS: No repeated characters\n");
    return 0;
}


// --------------------------------------------------
// TEST CASE 6: REPEATED CHARACTERS
// --------------------------------------------------

int testRepeatedCharacters()
{
    const std::uint8_t input[] = "AAAAAAAAAA";
    const std::uint32_t inputSize = sizeof(input) - 1;

    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, input, inputSize))
    {
        std::printf("FAIL: Repeated characters\n");
        return 1;
    }

    bool foundMatch = false;

    for (std::uint32_t i = 0; i < block.n; ++i)
    {
        if (cmp::isMatch(block.tok[i]))
        {
            foundMatch = true;

            if (cmp::distOf(block.tok[i]) == 0 ||
                cmp::lenOf(block.tok[i]) < cmp::kMinMatch)
            {
                std::printf("FAIL: Invalid repeated-character match\n");
                return 1;
            }

            break;
        }
    }

    if (!foundMatch)
    {
        std::printf("FAIL: No match found in repeated input\n");
        return 1;
    }

    std::printf("PASS: Repeated characters\n");
    return 0;
}


// --------------------------------------------------
// TEST CASE 7: FREQUENCY CORRECTNESS
// --------------------------------------------------

int testFrequencyCorrectness()
{
    const std::uint8_t input[] = "ABABABABABABABCD";
    const std::uint32_t inputSize = sizeof(input) - 1;

    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, input, inputSize))
    {
        std::printf("FAIL: Frequency correctness\n");
        return 1;
    }

    // Literal frequencies
    if (block.litlenFreq['A'] != 1 ||
        block.litlenFreq['B'] != 1 ||
        block.litlenFreq['C'] != 1 ||
        block.litlenFreq['D'] != 1)
    {
        std::printf("FAIL: Incorrect literal frequencies\n");
        return 1;
    }

    // Length 12 maps to symbol 265.
    if (block.litlenFreq[265] != 1)
    {
        std::printf("FAIL: Incorrect length frequency\n");
        return 1;
    }

    // End-of-block frequency
    if (block.litlenFreq[cmp::kEob] != 1)
    {
        std::printf("FAIL: Incorrect EOB frequency\n");
        return 1;
    }

    // Distance 2 maps to distance symbol 1.
    if (block.distFreq[1] != 1)
    {
        std::printf("FAIL: Incorrect distance frequency\n");
        return 1;
    }

    // Length 12 requires one extra bit.
    if (block.extraBitsTotal != 1)
    {
        std::printf("FAIL: Incorrect extra bits count\n");
        return 1;
    }

    std::printf("PASS: Frequency correctness\n");
    return 0;
}


// --------------------------------------------------
// TEST CASE 8: INVALID INPUT POINTER
// --------------------------------------------------

int testInvalidInputPointer()
{
    cmp::TokenBlock block{};

    bool result = cmp::compressLZ77(
        block,
        nullptr,
        10
    );

    if (result)
    {
        std::printf("FAIL: Invalid input pointer was accepted\n");
        return 1;
    }

    std::printf("PASS: Invalid input pointer rejected\n");
    return 0;
}

// --------------------------------------------------
// MAIN
// --------------------------------------------------

int main(int argc, char* argv[])
{
    // Check command-line arguments.
    if (argc != 2)
    {
        std::printf("Usage: ./test_lz77 <filename>\n");
        return 1;
    }

    // Open external file in binary mode.
    std::ifstream file(argv[1], std::ios::binary);

    if (!file)
    {
        std::printf("ERROR: Could not open file: %s\n", argv[1]);
        return 1;
    }

    // Read the complete file.
    std::vector<std::uint8_t> input(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    file.close();

    // Validate input size.
    if (input.size() >
        static_cast<std::size_t>(
            std::numeric_limits<std::int32_t>::max()))
    {
        std::printf("ERROR: File is too large.\n");
        return 1;
    }

    const std::uint32_t inputSize =
        static_cast<std::uint32_t>(input.size());

    const std::uint8_t* inputData =
        input.empty() ? nullptr : input.data();

    // Generate tokens and frequency information.
    displayTokensAndFrequencies(
        inputData,
        inputSize,
        argv[1]
    );

    // --------------------------------------------------
    // TEST RESULTS
    // --------------------------------------------------

    std::printf("\n");
    std::printf("============================================================\n");
    std::printf("                       TEST RESULTS\n");
    std::printf("============================================================\n");

     int failures = 0;

failures += testBasicCompression();
failures += testEmptyInput();
failures += testSingleByteInput();

failures += testMinimumMatchLength();
failures += testNoRepeatedCharacters();
failures += testRepeatedCharacters();
failures += testFrequencyCorrectness();
failures += testInvalidInputPointer();
    if (failures == 0)
    {
        std::printf("\nAll tests passed!\n");
        return 0;
    }

    std::printf("\n%d test(s) failed.\n", failures);
    return 1;
}
