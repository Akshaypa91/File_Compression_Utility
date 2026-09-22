#pragma once

#include "huffman_types.h"

struct HuffmanNode {
    HuffmanSymbol symbol;
    uint64_t frequency;

    HuffmanNode* left;
    HuffmanNode* right;

    bool isLeaf;
};