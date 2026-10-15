#include "../include/HashChain.h"
#include <stdlib.h>

int initHashChain(
    HashChain* chain,
    int inputSize,
    int hashSize
)
{
    chain->head = 0;
    chain->prev = 0;

    chain->hashSize = hashSize;
    chain->inputCapacity = inputSize;

    chain->head =
        (int*)malloc(hashSize * sizeof(int));

    if (chain->head == 0)
    {
        return 0;
    }

    chain->prev =
        (int*)malloc(inputSize * sizeof(int));

    if (chain->prev == 0)
    {
        free(chain->head);
        chain->head = 0;

        return 0;
    }

    for (int i = 0; i < hashSize; i++)
    {
        chain->head[i] = -1;
    }

    for (int i = 0; i < inputSize; i++)
    {
        chain->prev[i] = -1;
    }

    return 1;
}

void freeHashChain(HashChain* chain)
{
    free(chain->head);
    free(chain->prev);

    chain->head = 0;
    chain->prev = 0;

    chain->hashSize = 0;
    chain->inputCapacity = 0;
}

void clearHashChain(HashChain* chain)
{
    for (int i = 0; i < chain->hashSize; i++)
    {
        chain->head[i] = -1;
    }

    for (int i = 0; i < chain->inputCapacity; i++)
    {
        chain->prev[i] = -1;
    }
}

void insertHashPosition(
    HashChain* chain,
    int hashValue,
    int position
)
{
    chain->prev[position] =
        chain->head[hashValue];

    chain->head[hashValue] =
        position;
}

int getHashHead(
    HashChain* chain,
    int hashValue
)
{
    return chain->head[hashValue];
}

int getPreviousPosition(
    HashChain* chain,
    int position
)
{
    return chain->prev[position];
}