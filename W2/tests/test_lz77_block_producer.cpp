
#include "LZ77BlockProducer.h"
#include "LZ77.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    cmp::LZ77BlockProducer producer;
    cmp::TokenBlock block{};

    // Test 1: Normal input
    const std::uint8_t input[] = {
        'A', 'B', 'A', 'B', 'A', 'B', 'A', 'B'
    };

    producer.encodeBlock(
        input,
        sizeof(input),
        LZ77_WINDOW_SIZE,
        block
    );

    assert(block.src == input);
    assert(block.srcLen == sizeof(input));
    assert(block.n > 0);

    std::cout << "PASS: Adapter processes normal input\n";

    // Test 2: Input larger than one block
    std::vector<std::uint8_t> largeInput(
        cmp::kBlockTokens + 1, 'A'
    );

    producer.encodeBlock(
        largeInput.data(),
        largeInput.size(),
        LZ77_WINDOW_SIZE,
        block
    );

    assert(block.src == largeInput.data());
    assert(block.srcLen == cmp::kBlockTokens);
    assert(block.n > 0);

    std::cout << "PASS: Adapter respects block capacity\n";
    std::cout << "All adapter tests passed!\n";

    return 0;
}
