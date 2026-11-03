
#include <iostream>
#include <cassert>

#include "../include/HashChain.h"

int main()
{
    HashChain chain{};

    // -----------------------------------------
    // TEST 1: Initialization
    // -----------------------------------------

    assert(initHashChain(&chain, 10, 8));

    // Initially, all buckets and positions should be empty.
    for (int i = 0; i < 8; i++)
    {
        assert(getHashHead(&chain, i) == -1);
    }

    for (int i = 0; i < 10; i++)
    {
        assert(getPreviousPosition(&chain, i) == -1);
    }

    std::cout << "PASS: Hash chain initialization\n";


    // -----------------------------------------
    // TEST 2: Multiple Insertions
    // -----------------------------------------

    // Insert three positions into the same bucket.
    insertHashPosition(&chain, 2, 3);
    insertHashPosition(&chain, 2, 5);
    insertHashPosition(&chain, 2, 7);

    // The latest inserted position becomes the head.
    assert(getHashHead(&chain, 2) == 7);

    // Check whether the previous positions are linked correctly.
    assert(getPreviousPosition(&chain, 7) == 5);
    assert(getPreviousPosition(&chain, 5) == 3);
    assert(getPreviousPosition(&chain, 3) == -1);

    std::cout << "PASS: Multiple insertions and linking\n";


    // -----------------------------------------
    // TEST 3: Independent Hash Buckets
    // -----------------------------------------

    // Insert positions into another hash bucket.
    insertHashPosition(&chain, 4, 6);
    insertHashPosition(&chain, 4, 8);

    assert(getHashHead(&chain, 4) == 8);
    assert(getPreviousPosition(&chain, 8) == 6);

    // The earlier bucket should remain unaffected.
    assert(getHashHead(&chain, 2) == 7);
    assert(getPreviousPosition(&chain, 7) == 5);

    std::cout << "PASS: Independent hash buckets\n";


    // -----------------------------------------
    // TEST 4: Hash Collision Handling
    // -----------------------------------------

    // Insert two positions into the same hash bucket.
    // This simulates two sequences having the same hash value.
    insertHashPosition(&chain, 6, 1);
    insertHashPosition(&chain, 6, 9);

    assert(getHashHead(&chain, 6) == 9);
    assert(getPreviousPosition(&chain, 9) == 1);
    assert(getPreviousPosition(&chain, 1) == -1);

    std::cout << "PASS: Hash collision handling\n";


    // -----------------------------------------
    // TEST 5: Clear Hash Chain
    // -----------------------------------------

    clearHashChain(&chain);

    // After clearing, all buckets and positions
    // should return to their initial state.
    for (int i = 0; i < 8; i++)
    {
        assert(getHashHead(&chain, i) == -1);
    }

    for (int i = 0; i < 10; i++)
    {
        assert(getPreviousPosition(&chain, i) == -1);
    }

    std::cout << "PASS: Clear hash chain\n";


    // -----------------------------------------
    // TEST 6: Reset and Reuse
    // -----------------------------------------

    // Use the same allocated hash chain again.
    insertHashPosition(&chain, 3, 2);
    insertHashPosition(&chain, 3, 4);

    assert(getHashHead(&chain, 3) == 4);
    assert(getPreviousPosition(&chain, 4) == 2);

    // Clear it once more before starting a fresh chain.
    clearHashChain(&chain);

    assert(getHashHead(&chain, 3) == -1);
    assert(getPreviousPosition(&chain, 4) == -1);

    // Reuse the same memory with different positions.
    insertHashPosition(&chain, 5, 6);
    insertHashPosition(&chain, 5, 8);

    assert(getHashHead(&chain, 5) == 8);
    assert(getPreviousPosition(&chain, 8) == 6);
    assert(getPreviousPosition(&chain, 6) == -1);

    std::cout << "PASS: Reset and reuse hash chain\n";


    // -----------------------------------------
    // CLEANUP
    // -----------------------------------------

    freeHashChain(&chain);

    std::cout << "\nAll 6 Hash Chain tests passed!\n";

    return 0;
}
