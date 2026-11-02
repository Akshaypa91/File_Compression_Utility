#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../include/LZ77.h"
#include "../include/Token.h"
#include "../shared/include/symbols.hpp"

using namespace std;

int testBasicCompression()
{
    const uint8_t input[] = "ABABABABABABABCD";
    const uint32_t inputSize =
        static_cast<uint32_t>(sizeof(input) - 1);

    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, input, inputSize)) {
        printf("FAIL: Basic compression failed.\n");
        return 1;
    }

    if (block.n == 0) {
        printf("FAIL: No compact words were generated.\n");
        return 1;
    }

    if (block.src != input || block.srcLen != inputSize) {
        printf("FAIL: Source information is incorrect.\n");
        return 1;
    }

    if (block.litlenFreq[cmp::kEob] != 1) {
        printf("FAIL: End-of-block frequency should be 1.\n");
        return 1;
    }

    bool foundMatch = false;

    for (uint32_t i = 0; i < block.n; ++i) {
        if (cmp::isMatch(block.tok[i])) {
            foundMatch = true;
            break;
        }
    }

    if (!foundMatch) {
        printf("FAIL: Expected a match for repeated input.\n");
        return 1;
    }

    printf("PASS: Basic compression\n");
    return 0;
}

int testEmptyInput()
{
    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, nullptr, 0)) {
        printf("FAIL: Empty input compression failed.\n");
        return 1;
    }

    if (block.n != 0) {
        printf("FAIL: Empty input should generate no data words.\n");
        return 1;
    }

    if (block.litlenFreq[cmp::kEob] != 1) {
        printf("FAIL: Empty input must still have EOB frequency 1.\n");
        return 1;
    }

    printf("PASS: Empty input\n");
    return 0;
}

int testSingleByteInput()
{
    const uint8_t input[] = {'A'};
    cmp::TokenBlock block{};

    if (!cmp::compressLZ77(block, input, 1)) {
        printf("FAIL: Single-byte compression failed.\n");
        return 1;
    }

    if (block.n != 1) {
        printf("FAIL: Single byte should generate one compact word.\n");
        return 1;
    }

    if (cmp::isMatch(block.tok[0])) {
        printf("FAIL: Single byte should be a literal, not a match.\n");
        return 1;
    }

    if (cmp::litOf(block.tok[0]) != 'A') {
        printf("FAIL: Incorrect literal value.\n");
        return 1;
    }

    printf("PASS: Single-byte input\n");
    return 0;
}

int main()
{
    int failures = 0;

    failures += testBasicCompression();
    failures += testEmptyInput();
    failures += testSingleByteInput();

    if (failures == 0) {
        printf("\nAll tests passed!\n");
        return 0;
    }

    printf("\n%d test(s) failed.\n", failures);
    return 1;
}