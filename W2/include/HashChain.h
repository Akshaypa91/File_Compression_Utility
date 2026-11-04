#ifndef HASH_CHAIN_H
#define HASH_CHAIN_H

#include <stdint.h>

struct HashChain {
    int* head;
    int* prev;

    int hashSize;
    int inputCapacity;
};

int initHashChain(
    HashChain* chain,
    int inputSize,
    int hashSize
);

void freeHashChain(
    HashChain* chain
);

void clearHashChain(
    HashChain* chain
);

void insertHashPosition(
    HashChain* chain,
    int hashValue,
    int position
);

int getHashHead(
    HashChain* chain,
    int hashValue
);

int getPreviousPosition(
    HashChain* chain,
    int position
);

#endif