#pragma once

#include <cstdint>
#include <vector>

struct HuffmanSymbol {
    uint8_t type;
    uint32_t value;
};

struct HuffmanCode {
    uint32_t code;
    uint8_t bitLength;
};

struct CodeTableEntry {
    HuffmanSymbol symbol;
    HuffmanCode code;
};

using HuffmanCodeTable = std::vector<CodeTableEntry>;

struct EncodedBits {
    std::vector<uint8_t> packedBytes;
    size_t bitLength;
};

struct HuffmanOutput {
    HuffmanCodeTable codeTable;
    EncodedBits encodedBits;
};