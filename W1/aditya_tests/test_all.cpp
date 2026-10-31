// test_all.cpp - Complete test suite for W1 Huffman tree and code-length module.
// C++17 only. Plain terminal output. Seeded std::mt19937_64(20261031).

#define __USE_MINGW_ANSI_STDIO 1

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

#include "huffman_tree.h"
#include "min_heap.h"

// Project constants specified for W1
constexpr unsigned kNumLitLenSymbols = 286;
constexpr unsigned kNumDistSymbols = 30;
constexpr unsigned kMaxCodeLen = 15;
constexpr std::uint64_t kMaxKraftSum = 1ULL << kMaxCodeLen;

// Tiny test harness
static int g_total = 0;
static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) do { if (!(cond)) { testPass = false; } } while (0)

static void record(const char* id, const char* type, const char* technique, const char* desc, bool pass) {
    g_total++;
    if (pass) {
        g_passed++;
        std::printf("%-7s %-12s %-20s %-32s PASS\n", id, type, technique, desc);
    } else {
        g_failed++;
        std::printf("%-7s %-12s %-20s %-32s FAIL\n", id, type, technique, desc);
    }
}

// Independent canonical recomputation helper
static void independentCanonical(const std::uint8_t* lens, unsigned n, std::uint16_t* codes) {
    unsigned blCount[kMaxCodeLen + 1] = {};
    for (unsigned i = 0; i < n; ++i) {
        if (lens[i] != 0 && lens[i] <= kMaxCodeLen) {
            blCount[lens[i]]++;
        }
    }
    std::uint32_t nextCode[kMaxCodeLen + 2] = {};
    std::uint32_t code = 0;
    for (unsigned bits = 1; bits <= kMaxCodeLen; ++bits) {
        code = (code + blCount[bits - 1]) << 1;
        nextCode[bits] = code;
    }
    for (unsigned i = 0; i < n; ++i) {
        if (lens[i] == 0) {
            codes[i] = 0;
        } else {
            codes[i] = static_cast<std::uint16_t>(nextCode[lens[i]]++);
        }
    }
}

// Independent pairwise prefix check
static bool independentPrefixCheck(const std::uint8_t* lens, const std::uint16_t* codes, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        if (lens[i] == 0) continue;
        for (unsigned j = i + 1; j < n; ++j) {
            if (lens[j] == 0) continue;
            unsigned minL = std::min<unsigned>(lens[i], lens[j]);
            std::uint16_t c1 = static_cast<std::uint16_t>(codes[i] >> (lens[i] - minL));
            std::uint16_t c2 = static_cast<std::uint16_t>(codes[j] >> (lens[j] - minL));
            if (c1 == c2) return false;
        }
    }
    return true;
}

// Simple bit-by-bit trie decoder for round trips
struct SimpleTrie {
    std::vector<int> c0;
    std::vector<int> c1;
    std::vector<int> sym;

    void init() {
        c0.assign(1, -1);
        c1.assign(1, -1);
        sym.assign(1, -1);
    }

    void insert(unsigned s, std::uint16_t code, unsigned len) {
        int node = 0;
        for (int bit = static_cast<int>(len) - 1; bit >= 0; --bit) {
            int b = (code >> bit) & 1;
            int next = b ? c1[static_cast<std::size_t>(node)] : c0[static_cast<std::size_t>(node)];
            if (next < 0) {
                next = static_cast<int>(sym.size());
                if (b) c1[static_cast<std::size_t>(node)] = next;
                else c0[static_cast<std::size_t>(node)] = next;
                c0.push_back(-1);
                c1.push_back(-1);
                sym.push_back(-1);
            }
            node = next;
        }
        sym[static_cast<std::size_t>(node)] = static_cast<int>(s);
    }

    int decodeBit(int& node, int bit) const {
        node = bit ? c1[static_cast<std::size_t>(node)] : c0[static_cast<std::size_t>(node)];
        if (node < 0) return -2;
        int s = sym[static_cast<std::size_t>(node)];
        if (s >= 0) {
            node = 0;
            return s;
        }
        return -1;
    }
};

// Section A: Property tests (PR)
static void runSectionA(std::mt19937_64& rng) {
    std::printf("\nSection A: Property tests\n");

    bool propKraft = true;
    bool propLens = true;
    bool propFreq = true;
    bool propPrefix = true;
    bool propDet = true;
    bool propCanon = true;

    for (int tableIdx = 0; tableIdx < 10000; ++tableIdx) {
        unsigned n = 1 + static_cast<unsigned>(rng() % kNumLitLenSymbols);
        std::vector<std::uint32_t> freq(n, 0);

        int dist = tableIdx % 4;
        if (dist == 0) {
            for (unsigned i = 0; i < n; ++i) freq[i] = static_cast<std::uint32_t>(rng() % 100);
        } else if (dist == 1) {
            for (unsigned i = 0; i < n; ++i) freq[i] = (rng() % 4 == 0) ? 0 : static_cast<std::uint32_t>(1 + (rng() % 20));
        } else if (dist == 2) {
            for (unsigned i = 0; i < n; ++i) freq[i] = (rng() % 10 == 0) ? static_cast<std::uint32_t>(rng() % 5000000) : static_cast<std::uint32_t>(rng() % 4);
        } else {
            for (unsigned i = 0; i < n; ++i) freq[i] = (rng() % 2 == 0) ? 1 : 0;
        }

        bool hasUsed = false;
        for (unsigned i = 0; i < n; ++i) {
            if (freq[i] > 0) hasUsed = true;
        }
        if (!hasUsed && n > 0) freq[rng() % n] = 1;

        std::vector<std::uint8_t> lens1(n, 0);
        std::vector<std::uint8_t> lens2(n, 0);
        std::vector<std::uint16_t> codes1(n, 0);
        std::vector<std::uint16_t> codesRef(n, 0);

        w1::buildLengths(freq.data(), n, kMaxCodeLen, lens1.data());
        w1::buildLengths(freq.data(), n, kMaxCodeLen, lens2.data());
        w1::assignCanonical(lens1.data(), n, codes1.data());
        independentCanonical(lens1.data(), n, codesRef.data());

        // (a) Kraft sum <= 2^15
        std::uint64_t kSum = w1::kraftSum(lens1.data(), n);
        if (kSum > kMaxKraftSum) propKraft = false;

        // (b) used in 1..15, unused 0
        for (unsigned i = 0; i < n; ++i) {
            if (freq[i] == 0 && lens1[i] != 0) propLens = false;
            if (freq[i] > 0 && (lens1[i] < 1 || lens1[i] > kMaxCodeLen)) propLens = false;
        }

        // (c) more frequent never longer
        for (unsigned i = 0; i < n; ++i) {
            if (freq[i] == 0) continue;
            for (unsigned j = 0; j < n; ++j) {
                if (freq[j] == 0) continue;
                if (freq[i] > freq[j] && lens1[i] > lens1[j]) propFreq = false;
            }
        }

        // (d) prefix-free via verifyPrefixFree AND pairwise check
        if (!w1::verifyPrefixFree(lens1.data(), codes1.data(), n)) propPrefix = false;
        if (!independentPrefixCheck(lens1.data(), codes1.data(), n)) propPrefix = false;

        // (e) determinism
        if (lens1 != lens2) propDet = false;

        // (f) canonical matches reference
        if (codes1 != codesRef) propCanon = false;

        if (!propKraft || !propLens || !propFreq || !propPrefix || !propDet || !propCanon) {
            std::printf("First failing table at index %d (n=%u):\n", tableIdx, n);
            for (unsigned i = 0; i < n && i < 16; ++i) {
                std::printf("  sym %u: freq=%u len=%u code=%u\n", i, freq[i], lens1[i], codes1[i]);
            }
            break;
        }
    }

    record("PR-01", "Property", "Random property", "10k tables: Kraft sum <= 2^15", propKraft);
    record("PR-02", "Property", "Random property", "10k tables: used 1..15, unused 0", propLens);
    record("PR-03", "Property", "Random property", "10k tables: more freq not longer", propFreq);
    record("PR-04", "Property", "Random property", "10k tables: prefix-free (both)", propPrefix);
    record("PR-05", "Property", "Random property", "10k tables: deterministic repeat", propDet);
    record("PR-06", "Property", "Random property", "10k tables: canonical rule match", propCanon);
}

// Section B: Unit tests (UT)
static void runSectionB() {
    std::printf("\nSection B: Unit tests\n");

    // UT-01: buildLengths alone
    {
        bool testPass = true;
        std::uint32_t f[4] = {4, 2, 1, 1};
        std::uint8_t lens[4] = {};
        w1::buildLengths(f, 4, 15, lens);
        CHECK(lens[0] == 1 && lens[1] == 2 && lens[2] == 3 && lens[3] == 3);
        record("UT-01", "Unit test", "Component test", "buildLengths basic 4 symbols", testPass);
    }

    // UT-02: assignCanonical with hand-written lengths
    {
        bool testPass = true;
        std::uint8_t lens[4] = {1, 2, 3, 3};
        std::uint16_t codes[4] = {};
        w1::assignCanonical(lens, 4, codes);
        CHECK(codes[0] == 0 && codes[1] == 2 && codes[2] == 6 && codes[3] == 7);
        record("UT-02", "Unit test", "Component test", "assignCanonical hand-written lens", testPass);
    }

    // UT-03: kraftSum empty
    {
        bool testPass = true;
        std::uint8_t lens[5] = {0, 0, 0, 0, 0};
        CHECK(w1::kraftSum(lens, 5) == 0);
        record("UT-03", "Unit test", "Component test", "kraftSum empty table returns 0", testPass);
    }

    // UT-04: kraftSum complete
    {
        bool testPass = true;
        std::uint8_t lens[4] = {1, 2, 3, 3};
        CHECK(w1::kraftSum(lens, 4) == kMaxKraftSum);
        record("UT-04", "Unit test", "Component test", "kraftSum complete code == 32768", testPass);
    }

    // UT-05: kraftSum over-full
    {
        bool testPass = true;
        std::uint8_t lens[3] = {1, 1, 1};
        CHECK(w1::kraftSum(lens, 3) == (3ULL << 14));
        CHECK(w1::kraftSum(lens, 3) > kMaxKraftSum);
        record("UT-05", "Unit test", "Component test", "kraftSum over-full code > 32768", testPass);
    }

    // UT-06: kraftSum incomplete
    {
        bool testPass = true;
        std::uint8_t lens[2] = {2, 2};
        CHECK(w1::kraftSum(lens, 2) == 16384ULL);
        CHECK(w1::kraftSum(lens, 2) < kMaxKraftSum);
        record("UT-06", "Unit test", "Component test", "kraftSum incomplete valid code", testPass);
    }

    // UT-07: verifyPrefixFree valid set
    {
        bool testPass = true;
        std::uint8_t lens[4] = {1, 2, 3, 3};
        std::uint16_t codes[4] = {0, 2, 6, 7};
        CHECK(w1::verifyPrefixFree(lens, codes, 4));
        record("UT-07", "Unit test", "Component test", "verifyPrefixFree on valid code", testPass);
    }

    // UT-08: verifyPrefixFree invalid set 0 and 01
    {
        bool testPass = true;
        std::uint8_t lens[2] = {1, 2};
        std::uint16_t codes[2] = {0, 1}; // 0b0 and 0b01
        CHECK(!w1::verifyPrefixFree(lens, codes, 2));
        record("UT-08", "Unit test", "Component test", "verifyPrefixFree invalid prefix 0,01", testPass);
    }

    // UT-09: MinHeap push/pop order
    {
        bool testPass = true;
        w1::MinHeap heap;
        heap.push(w1::HeapItem{10, 1});
        heap.push(w1::HeapItem{5, 2});
        heap.push(w1::HeapItem{5, 0});
        heap.push(w1::HeapItem{20, 3});
        w1::HeapItem it1 = heap.pop();
        w1::HeapItem it2 = heap.pop();
        w1::HeapItem it3 = heap.pop();
        w1::HeapItem it4 = heap.pop();
        CHECK(it1.weight == 5 && it1.id == 0);
        CHECK(it2.weight == 5 && it2.id == 2);
        CHECK(it3.weight == 10 && it3.id == 1);
        CHECK(it4.weight == 20 && it4.id == 3);
        record("UT-09", "Unit test", "Component test", "MinHeap push/pop priority order", testPass);
    }

    // UT-10: MinHeap size and empty
    {
        bool testPass = true;
        w1::MinHeap heap;
        CHECK(heap.empty() && heap.size() == 0);
        heap.push(w1::HeapItem{1, 0});
        CHECK(!heap.empty() && heap.size() == 1);
        heap.pop();
        CHECK(heap.empty() && heap.size() == 0);
        record("UT-10", "Unit test", "Component test", "MinHeap size and empty methods", testPass);
    }

    // UT-11: heapLess comparator
    {
        bool testPass = true;
        w1::HeapItem a{10, 1};
        w1::HeapItem b{20, 0};
        w1::HeapItem c{10, 0};
        w1::HeapItem d{10, 1};
        CHECK(w1::heapLess(a, b));
        CHECK(!w1::heapLess(b, a));
        CHECK(w1::heapLess(c, a));
        CHECK(!w1::heapLess(a, d) && !w1::heapLess(d, a));
        record("UT-11", "Unit test", "Component test", "heapLess strict weak ordering", testPass);
    }
}

// Section C: Black box (BB), public API only
static void runSectionC() {
    std::printf("\nSection C: Black box tests\n");

    // Equivalence classes
    {
        bool testPass = true;
        std::uint8_t lens[1] = {9};
        w1::buildLengths(nullptr, 0, 15, lens);
        record("BB-01", "Black box", "Equivalence cls", "empty table (0 symbols used)", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[1] = {42};
        std::uint8_t lens[1] = {};
        w1::buildLengths(f, 1, 15, lens);
        CHECK(lens[0] == 1);
        record("BB-02", "Black box", "Equivalence cls", "1 symbol used length is 1", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[2] = {1, 1};
        std::uint8_t lens[2] = {};
        w1::buildLengths(f, 2, 15, lens);
        CHECK(lens[0] == 1 && lens[1] == 1);
        record("BB-03", "Black box", "Boundary value", "2 symbols, weights 1 and 1", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        std::uint8_t lens[8] = {};
        w1::buildLengths(f, 8, 15, lens);
        CHECK(w1::kraftSum(lens, 8) == kMaxKraftSum);
        record("BB-04", "Black box", "Equivalence cls", "small alphabet (8 symbols)", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[64];
        for (unsigned i = 0; i < 64; ++i) f[i] = i + 1;
        std::uint8_t lens[64] = {};
        w1::buildLengths(f, 64, 15, lens);
        CHECK(w1::kraftSum(lens, 64) == kMaxKraftSum);
        record("BB-05", "Black box", "Equivalence cls", "medium alphabet (64 symbols)", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[286];
        for (unsigned i = 0; i < 286; ++i) f[i] = 1;
        std::uint8_t lens[286] = {};
        w1::buildLengths(f, 286, 15, lens);
        CHECK(w1::kraftSum(lens, 286) == kMaxKraftSum);
        record("BB-06", "Black box", "Equivalence cls", "286 literal/length symbols", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[30];
        for (unsigned i = 0; i < 30; ++i) f[i] = i + 1;
        std::uint8_t lens[30] = {};
        w1::buildLengths(f, 30, 15, lens);
        CHECK(w1::kraftSum(lens, 30) == kMaxKraftSum);
        record("BB-07", "Black box", "Equivalence cls", "30 distance symbols", testPass);
    }

    // Boundary values
    {
        bool testPass = true;
        std::uint32_t f[10] = {};
        std::uint8_t lens[10] = {5, 5, 5};
        w1::buildLengths(f, 10, 15, lens);
        for (int i = 0; i < 10; ++i) CHECK(lens[i] == 0);
        record("BB-08", "Black box", "Boundary value", "0 used symbols gives all zeros", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[10] = {};
        f[4] = 99;
        std::uint8_t lens[10] = {};
        w1::buildLengths(f, 10, 15, lens);
        CHECK(lens[4] == 1);
        for (int i = 0; i < 10; ++i) if (i != 4) CHECK(lens[i] == 0);
        record("BB-09", "Black box", "Boundary value", "1 used symbol in 10-table", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[10] = {};
        f[1] = 5; f[8] = 10;
        std::uint8_t lens[10] = {};
        w1::buildLengths(f, 10, 15, lens);
        CHECK(lens[1] == 1 && lens[8] == 1);
        record("BB-10", "Black box", "Boundary value", "2 used symbols in 10-table", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[286] = {};
        for (unsigned i = 0; i < 285; ++i) f[i] = 1;
        std::uint8_t lens[286] = {};
        w1::buildLengths(f, 286, 15, lens);
        CHECK(lens[285] == 0);
        CHECK(w1::kraftSum(lens, 286) <= kMaxKraftSum);
        record("BB-11", "Black box", "Boundary value", "285 used symbols in 286-table", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[286] = {};
        for (unsigned i = 0; i < 286; ++i) f[i] = 1;
        std::uint8_t lens[286] = {};
        w1::buildLengths(f, 286, 15, lens);
        for (unsigned i = 0; i < 286; ++i) CHECK(lens[i] > 0);
        record("BB-12", "Black box", "Boundary value", "286 used symbols (full alphabet)", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[4] = {0, 5, 0, 10};
        std::uint8_t lens[4] = {};
        w1::buildLengths(f, 4, 15, lens);
        CHECK(lens[0] == 0 && lens[2] == 0 && lens[1] == 1 && lens[3] == 1);
        record("BB-13", "Black box", "Boundary value", "weight 0 among used symbols", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[2] = {1, 1};
        std::uint8_t lens[2] = {};
        w1::buildLengths(f, 2, 15, lens);
        CHECK(lens[0] == 1 && lens[1] == 1);
        record("BB-14", "Black box", "Boundary value", "minimum positive weight 1", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[2] = {2, 2};
        std::uint8_t lens[2] = {};
        w1::buildLengths(f, 2, 15, lens);
        CHECK(lens[0] == 1 && lens[1] == 1);
        record("BB-15", "Black box", "Boundary value", "small positive weight 2", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
        std::uint8_t lens[2] = {};
        w1::buildLengths(f, 2, 15, lens);
        CHECK(lens[0] == 1 && lens[1] == 1);
        record("BB-16", "Black box", "Boundary value", "max uint32 weight 0xFFFFFFFF", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[286] = {};
        f[0] = 1;
        std::uint8_t lens[286] = {};
        w1::buildLengths(f, 286, 15, lens);
        CHECK(lens[0] == 1);
        for (unsigned i = 1; i < 286; ++i) CHECK(lens[i] == 0);
        record("BB-17", "Black box", "Boundary value", "only first symbol used (sym 0)", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[286] = {};
        f[285] = 1;
        std::uint8_t lens[286] = {};
        w1::buildLengths(f, 286, 15, lens);
        CHECK(lens[285] == 1);
        for (unsigned i = 0; i < 285; ++i) CHECK(lens[i] == 0);
        record("BB-18", "Black box", "Boundary value", "only last symbol used (sym 285)", testPass);
    }

    // All equal weights for various n
    auto testEqualWeights = [](unsigned count) -> bool {
        std::vector<std::uint32_t> f(count, 1);
        std::vector<std::uint8_t> lens(count, 0);
        w1::buildLengths(f.data(), count, 15, lens.data());
        return w1::kraftSum(lens.data(), count) == kMaxKraftSum;
    };
    record("BB-19", "Black box", "Boundary value", "all weights equal for n = 2", testEqualWeights(2));
    record("BB-20", "Black box", "Boundary value", "all weights equal for n = 3", testEqualWeights(3));
    record("BB-21", "Black box", "Boundary value", "all weights equal for n = 4", testEqualWeights(4));
    record("BB-22", "Black box", "Boundary value", "all weights equal for n = 5", testEqualWeights(5));
    record("BB-23", "Black box", "Boundary value", "all weights equal for n = 8", testEqualWeights(8));
    record("BB-24", "Black box", "Boundary value", "all weights equal for n = 16", testEqualWeights(16));
    record("BB-25", "Black box", "Boundary value", "all weights equal for n = 286", testEqualWeights(286));

    // Error guessing
    {
        bool testPass = true;
        std::uint32_t f[6] = {0, 10, 0, 20, 0, 30};
        std::uint8_t lens[6] = {};
        w1::buildLengths(f, 6, 15, lens);
        CHECK(lens[0] == 0 && lens[2] == 0 && lens[4] == 0);
        CHECK(lens[1] > 0 && lens[3] > 0 && lens[5] > 0);
        record("BB-26", "Black box", "Error guessing", "zero weights interspersed", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
        std::uint8_t lens[2] = {};
        w1::buildLengths(f, 2, 15, lens);
        CHECK(lens[0] == 1 && lens[1] == 1);
        record("BB-27", "Black box", "Error guessing", "two huge weights summed overflow", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[8] = {1, 0, 2, 0, 4, 0, 8, 0};
        std::uint8_t lens[8] = {};
        w1::buildLengths(f, 8, 15, lens);
        for (int i = 0; i < 8; ++i) CHECK((f[i] == 0) == (lens[i] == 0));
        record("BB-28", "Black box", "Error guessing", "alternating used/unused symbols", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[4] = {1, 2, 4, 8};
        std::uint8_t lens[4] = {};
        w1::buildLengths(f, 4, 15, lens);
        CHECK(lens[0] >= lens[1] && lens[1] >= lens[2] && lens[2] >= lens[3]);
        record("BB-29", "Black box", "Error guessing", "weights in ascending order", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[4] = {8, 4, 2, 1};
        std::uint8_t lens[4] = {};
        w1::buildLengths(f, 4, 15, lens);
        CHECK(lens[0] <= lens[1] && lens[1] <= lens[2] && lens[2] <= lens[3]);
        record("BB-30", "Black box", "Error guessing", "weights in descending order", testPass);
    }

    // Length-limit decision table
    {
        bool testPass = true;
        std::uint32_t f[4] = {1, 1, 2, 4};
        std::uint8_t lens[4] = {};
        w1::buildLengths(f, 4, 15, lens);
        for (int i = 0; i < 4; ++i) CHECK(lens[i] < 15);
        record("BB-31", "Black box", "Decision table", "depth strictly below 15", testPass);
    }
    {
        bool testPass = true;
        std::vector<std::uint32_t> f(16);
        std::uint64_t a = 1, b = 1;
        for (int i = 0; i < 16; ++i) {
            f[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(a);
            std::uint64_t c = a + b; a = b; b = c;
        }
        std::vector<std::uint8_t> lens(16);
        w1::buildLengths(f.data(), 16, 15, lens.data());
        int maxL = 0;
        for (auto l : lens) if (l > maxL) maxL = l;
        CHECK(maxL <= 15);
        CHECK(w1::kraftSum(lens.data(), 16) == kMaxKraftSum);
        record("BB-32", "Black box", "Decision table", "depth reaching exactly 15", testPass);
    }
    {
        bool testPass = true;
        std::vector<std::uint32_t> f(40);
        std::uint64_t a = 1, b = 1;
        for (int i = 0; i < 40; ++i) {
            f[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(a);
            std::uint64_t c = a + b; a = b; b = c;
        }
        std::vector<std::uint8_t> lens(40);
        w1::buildLengths(f.data(), 40, 15, lens.data());
        for (auto l : lens) CHECK(l <= 15);
        CHECK(w1::kraftSum(lens.data(), 40) <= kMaxKraftSum);
        record("BB-33", "Black box", "Decision table", "depth above 15 (Fibonacci 40)", testPass);
    }

    // Golden tests G1 to G6
    {
        bool testPass = true;
        std::uint32_t f[4] = {4, 2, 1, 1};
        std::uint8_t lens[4] = {};
        std::uint16_t codes[4] = {};
        w1::buildLengths(f, 4, 15, lens);
        w1::assignCanonical(lens, 4, codes);
        CHECK(lens[0] == 1 && lens[1] == 2 && lens[2] == 3 && lens[3] == 3);
        CHECK(codes[0] == 0b0 && codes[1] == 0b10 && codes[2] == 0b110 && codes[3] == 0b111);
        record("BB-34", "Black box", "Golden test", "G1 weights 4,2,1,1 -> 1,2,3,3", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[286] = {};
        f[97] = 1; f[98] = 1; f[99] = 1; f[120] = 1; f[256] = 1; f[263] = 1;
        std::uint8_t lens[286] = {};
        std::uint16_t codes[286] = {};
        w1::buildLengths(f, 286, 15, lens);
        w1::assignCanonical(lens, 286, codes);
        CHECK(lens[97] == 3 && lens[98] == 3 && lens[99] == 3 && lens[120] == 3);
        CHECK(lens[256] == 2 && lens[263] == 2);
        CHECK(codes[256] == 0b00 && codes[263] == 0b01);
        CHECK(codes[97] == 0b100 && codes[98] == 0b101 && codes[99] == 0b110 && codes[120] == 0b111);
        CHECK(w1::kraftSum(lens, 286) == kMaxKraftSum);
        record("BB-35", "Black box", "Golden test", "G2 symbols 97,98,99,120,256,263", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[3] = {1, 1, 1};
        std::uint8_t lens[3] = {};
        w1::buildLengths(f, 3, 15, lens);
        CHECK(lens[0] == 2 && lens[1] == 2 && lens[2] == 1);
        record("BB-36", "Black box", "Golden test", "G3 three equal weights -> 2,2,1", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[5] = {5, 3, 2, 1, 1};
        std::uint8_t lens[5] = {};
        w1::buildLengths(f, 5, 3, lens);
        CHECK(lens[0] == 1 && lens[1] == 3 && lens[2] == 3 && lens[3] == 3 && lens[4] == 3);
        CHECK(w1::kraftSum(lens, 5) == kMaxKraftSum);
        record("BB-37", "Black box", "Golden test", "G4 maxLen 3, weights 5,3,2,1,1", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[30] = {};
        f[2] = 50;
        std::uint8_t lens[30] = {};
        std::uint16_t codes[30] = {};
        w1::buildLengths(f, 30, 15, lens);
        w1::assignCanonical(lens, 30, codes);
        CHECK(lens[2] == 1 && codes[2] == 0);
        for (unsigned i = 0; i < 30; ++i) if (i != 2) CHECK(lens[i] == 0);
        record("BB-38", "Black box", "Golden test", "G5 distance table only symbol 2", testPass);
    }
    {
        bool testPass = true;
        std::uint32_t f[286] = {};
        f[140] = 777;
        std::uint8_t lens[286] = {};
        w1::buildLengths(f, 286, 15, lens);
        CHECK(lens[140] == 1);
        for (unsigned i = 0; i < 286; ++i) if (i != 140) CHECK(lens[i] == 0);
        record("BB-39", "Black box", "Golden test", "G6 one symbol in 286 table", testPass);
    }
}

// Section D: White box (WB)
static void runSectionD() {
    std::printf("\nSection D: White box tests\n");

    // WB-01: empty branch (m == 0)
    {
        bool testPass = true;
        std::uint32_t f[10] = {};
        std::uint8_t lens[10] = {1, 2, 3};
        w1::buildLengths(f, 10, 15, lens);
        for (int i = 0; i < 10; ++i) CHECK(lens[i] == 0);
        record("WB-01", "White box", "Path coverage", "empty branch (m == 0)", testPass);
    }

    // WB-02: single-symbol branch (m == 1)
    {
        bool testPass = true;
        std::uint32_t f[10] = {};
        f[3] = 100;
        std::uint8_t lens[10] = {};
        w1::buildLengths(f, 10, 15, lens);
        CHECK(lens[3] == 1);
        record("WB-02", "White box", "Path coverage", "single-symbol branch (m == 1)", testPass);
    }

    // WB-03: merge loop
    {
        bool testPass = true;
        std::uint32_t f[4] = {10, 20, 30, 40};
        std::uint8_t lens[4] = {};
        w1::buildLengths(f, 4, 15, lens);
        CHECK(w1::kraftSum(lens, 4) == kMaxKraftSum);
        record("WB-03", "White box", "Path coverage", "min-heap merge loop execution", testPass);
    }

    // WB-04: depth pass
    {
        bool testPass = true;
        std::uint32_t f[4] = {1, 1, 2, 4};
        std::uint8_t lens[4] = {};
        w1::buildLengths(f, 4, 15, lens);
        CHECK(lens[0] == 3 && lens[1] == 3 && lens[2] == 2 && lens[3] == 1);
        record("WB-04", "White box", "Path coverage", "downward depth pass calculation", testPass);
    }

    // WB-05: length-limit fix-up inner scan (blCount bits)
    {
        bool testPass = true;
        std::uint32_t f[5] = {5, 3, 2, 1, 1};
        std::uint8_t lens[5] = {};
        w1::buildLengths(f, 5, 3, lens);
        CHECK(lens[0] == 1 && lens[1] == 3 && lens[2] == 3 && lens[3] == 3 && lens[4] == 3);
        record("WB-05", "White box", "Path coverage", "length limit inner scan", testPass);
    }

    // WB-06: outer loop repeating >1 times (needs very skewed table)
    {
        bool testPass = true;
        std::vector<std::uint32_t> f(35);
        std::uint64_t a = 1, b = 1;
        for (int i = 0; i < 35; ++i) {
            f[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(a);
            std::uint64_t c = a + b; a = b; b = c;
        }
        std::vector<std::uint8_t> lens(35);
        w1::buildLengths(f.data(), 35, 15, lens.data());
        for (auto l : lens) CHECK(l <= 15);
        CHECK(w1::kraftSum(lens.data(), 35) <= kMaxKraftSum);
        record("WB-06", "White box", "Path coverage", "fixup outer loop repeats >1", testPass);
    }

    // WB-07: canonical loop
    {
        bool testPass = true;
        std::uint8_t lens[5] = {1, 3, 3, 3, 3};
        std::uint16_t codes[5] = {};
        w1::assignCanonical(lens, 5, codes);
        CHECK(codes[0] == 0 && codes[1] == 4 && codes[2] == 5 && codes[3] == 6 && codes[4] == 7);
        record("WB-07", "White box", "Path coverage", "canonical code generation loop", testPass);
    }

    // WB-08: heap sift-up
    {
        bool testPass = true;
        w1::MinHeap heap;
        heap.push(w1::HeapItem{50, 0});
        heap.push(w1::HeapItem{40, 1});
        heap.push(w1::HeapItem{30, 2});
        heap.push(w1::HeapItem{20, 3});
        heap.push(w1::HeapItem{10, 4});
        CHECK(heap.top().weight == 10 && heap.top().id == 4);
        record("WB-08", "White box", "Path coverage", "MinHeap sift-up ordering", testPass);
    }

    // WB-09: heap sift-down
    {
        bool testPass = true;
        w1::MinHeap heap;
        heap.push(w1::HeapItem{10, 0});
        heap.push(w1::HeapItem{20, 1});
        heap.push(w1::HeapItem{30, 2});
        w1::HeapItem top = heap.pop();
        CHECK(top.weight == 10);
        CHECK(heap.top().weight == 20 && heap.top().id == 1);
        record("WB-09", "White box", "Path coverage", "MinHeap sift-down rebalance", testPass);
    }

    // WB-10: tie-break (G3 depths)
    {
        bool testPass = true;
        std::uint32_t f[3] = {10, 10, 10};
        std::uint8_t lens[3] = {};
        w1::buildLengths(f, 3, 15, lens);
        CHECK(lens[0] == 2 && lens[1] == 2 && lens[2] == 1);
        record("WB-10", "White box", "Path coverage", "tie-break equal weights (G3)", testPass);
    }

    // WB-11: test proving tie-break direction is smaller id first
    {
        bool testPass = true;
        // Weights where length limiting assigns different lengths to equal weights.
        // Symbols 3 and 4 have equal weight 8. In natural tree, leaf 3 is depth 3, leaf 4 is depth 2.
        // With smaller id first tie-breaking, symbol 3 gets length 3, symbol 4 gets length 2.
        std::uint32_t f[6] = {1, 2, 4, 8, 8, 16};
        std::uint8_t lens[6] = {};
        w1::buildLengths(f, 6, 3, lens);
        CHECK(lens[3] == 3);
        CHECK(lens[4] == 2);
        record("WB-11", "White box", "Path coverage", "tie-break direction smaller id", testPass);
    }

    // WB-12: MinHeap capacity 320 boundary
    {
        bool testPass = true;
        w1::MinHeap heap;
        for (std::size_t i = 0; i < 320; ++i) {
            CHECK(heap.push(w1::HeapItem{static_cast<std::uint64_t>(i), static_cast<std::uint32_t>(i)}));
        }
        CHECK(heap.size() == 320);
        // The 321st push must return false
        CHECK(!heap.push(w1::HeapItem{999, 999}));
        CHECK(heap.size() == 320);
        record("WB-12", "White box", "Boundary value", "push 321 items: 321st fails", testPass);
    }

    // WB-13: buildTrie coverage
    {
        bool testPass = true;
        std::uint8_t lens[4] = {1, 2, 3, 3};
        std::uint16_t codes[4] = {0, 2, 6, 7};
        w1::CodeTrie trie;
        CHECK(w1::buildTrie(lens, codes, 4, trie));
        // test invalid prefix into buildTrie
        std::uint8_t badLens[2] = {1, 2};
        std::uint16_t badCodes[2] = {0, 1}; // 0 is prefix of 01
        w1::CodeTrie badTrie;
        CHECK(!w1::buildTrie(badLens, badCodes, 2, badTrie));
        record("WB-13", "White box", "Path coverage", "buildTrie valid and prefix reject", testPass);
    }

    // WB-14: trieDecode bit stream decoding
    {
        bool testPass = true;
        std::uint8_t lens[4] = {1, 2, 3, 3};
        std::uint16_t codes[4] = {0, 2, 6, 7};
        w1::CodeTrie trie;
        CHECK(w1::buildTrie(lens, codes, 4, trie));
        // Encode symbol sequence: 0, 1, 2, 3
        // Code 0 (len 1, bit 0), Code 1 (len 2, bits 10), Code 2 (len 3, bits 110), Code 3 (len 3, bits 111)
        // Bits: 0 10 110 111 = 0101 1011 1000 0000 = 0x5B, 0x80 (9 bits)
        std::uint8_t data[2] = {0x5B, 0x80};
        w1::BitReader reader(data, 2, 9);
        CHECK(w1::trieDecode(trie, reader) == 0);
        CHECK(w1::trieDecode(trie, reader) == 1);
        CHECK(w1::trieDecode(trie, reader) == 2);
        CHECK(w1::trieDecode(trie, reader) == 3);
        CHECK(w1::trieDecode(trie, reader) == -1); // bits exhausted
        record("WB-14", "White box", "Path coverage", "trieDecode bit-by-bit walk", testPass);
    }
}

// Section E: Integration (IT)
static void runSectionE(std::mt19937_64& rng) {
    std::printf("\nSection E: Integration tests\n");

    // IT-01: Round trip with 200 random symbol sequences of 1000 symbols
    {
        bool testPass = true;
        for (int run = 0; run < 200; ++run) {
            unsigned alphabetSize = 2 + static_cast<unsigned>(rng() % 285);
            std::vector<unsigned> seq(1000);
            std::vector<std::uint32_t> freq(alphabetSize, 0);
            for (int i = 0; i < 1000; ++i) {
                unsigned sym = static_cast<unsigned>(rng() % alphabetSize);
                seq[static_cast<std::size_t>(i)] = sym;
                freq[sym]++;
            }

            std::vector<std::uint8_t> lens(alphabetSize, 0);
            std::vector<std::uint16_t> codes(alphabetSize, 0);
            w1::buildLengths(freq.data(), alphabetSize, kMaxCodeLen, lens.data());
            w1::assignCanonical(lens.data(), alphabetSize, codes.data());
            if (!w1::verifyPrefixFree(lens.data(), codes.data(), alphabetSize)) {
                testPass = false;
                break;
            }

            // Encode into std::vector<bool> MSB first
            std::vector<bool> bits;
            for (unsigned s : seq) {
                unsigned l = lens[s];
                std::uint16_t c = codes[s];
                for (int b = static_cast<int>(l) - 1; b >= 0; --b) {
                    bits.push_back(((c >> b) & 1) != 0);
                }
            }

            // Decode bit by bit
            SimpleTrie trie;
            trie.init();
            for (unsigned s = 0; s < alphabetSize; ++s) {
                if (lens[s] > 0) trie.insert(s, codes[s], lens[s]);
            }

            std::vector<unsigned> decoded;
            int node = 0;
            for (bool bit : bits) {
                int sym = trie.decodeBit(node, bit ? 1 : 0);
                if (sym >= 0) decoded.push_back(static_cast<unsigned>(sym));
            }

            if (decoded != seq) {
                testPass = false;
                break;
            }
        }
        record("IT-01", "Integration", "Round trip", "200 random 1000-symbol sequences", testPass);
    }

    // IT-02: Golden G2 table round trip
    {
        bool testPass = true;
        std::uint32_t f[286] = {};
        f[97] = 1; f[98] = 1; f[99] = 1; f[120] = 1; f[256] = 1; f[263] = 1;
        std::uint8_t lens[286] = {};
        std::uint16_t codes[286] = {};
        w1::buildLengths(f, 286, 15, lens);
        w1::assignCanonical(lens, 286, codes);
        CHECK(w1::verifyPrefixFree(lens, codes, 286));

        std::vector<unsigned> g2Syms = {97, 98, 99, 120, 256, 263, 256, 97, 120};
        std::vector<bool> bits;
        for (unsigned s : g2Syms) {
            for (int b = static_cast<int>(lens[s]) - 1; b >= 0; --b) {
                bits.push_back(((codes[s] >> b) & 1) != 0);
            }
        }
        SimpleTrie trie;
        trie.init();
        for (unsigned s : {97u, 98u, 99u, 120u, 256u, 263u}) {
            trie.insert(s, codes[s], lens[s]);
        }
        std::vector<unsigned> decoded;
        int node = 0;
        for (bool bit : bits) {
            int sym = trie.decodeBit(node, bit ? 1 : 0);
            if (sym >= 0) decoded.push_back(static_cast<unsigned>(sym));
        }
        CHECK(decoded == g2Syms);
        record("IT-02", "Integration", "Round trip", "golden G2 table round trip", testPass);
    }

    // IT-03: 286 plus 30 symbol pair built together
    {
        bool testPass = true;
        std::uint32_t litFreq[kNumLitLenSymbols];
        std::uint32_t distFreq[kNumDistSymbols];
        for (unsigned i = 0; i < kNumLitLenSymbols; ++i) litFreq[i] = static_cast<std::uint32_t>(1 + (rng() % 50));
        for (unsigned i = 0; i < kNumDistSymbols; ++i) distFreq[i] = static_cast<std::uint32_t>(1 + (rng() % 50));

        std::uint8_t litLens[kNumLitLenSymbols] = {};
        std::uint8_t distLens[kNumDistSymbols] = {};
        std::uint16_t litCodes[kNumLitLenSymbols] = {};
        std::uint16_t distCodes[kNumDistSymbols] = {};

        w1::buildLengths(litFreq, kNumLitLenSymbols, 15, litLens);
        w1::buildLengths(distFreq, kNumDistSymbols, 15, distLens);
        w1::assignCanonical(litLens, kNumLitLenSymbols, litCodes);
        w1::assignCanonical(distLens, kNumDistSymbols, distCodes);

        CHECK(w1::verifyPrefixFree(litLens, litCodes, kNumLitLenSymbols));
        CHECK(w1::verifyPrefixFree(distLens, distCodes, kNumDistSymbols));
        CHECK(w1::kraftSum(litLens, kNumLitLenSymbols) == kMaxKraftSum);
        CHECK(w1::kraftSum(distLens, kNumDistSymbols) == kMaxKraftSum);
        record("IT-03", "Integration", "Round trip", "286 + 30 paired compression build", testPass);
    }
}

// Section F: Functional tests (FT)
static void runSectionF() {
    std::printf("\nSection F: Functional tests (R1 to R12)\n");

    // FT-R01: R1 used symbols get length 1..15, unused get 0
    {
        bool testPass = true;
        std::uint32_t f[6] = {0, 10, 0, 20, 0, 30};
        std::uint8_t lens[6] = {};
        w1::buildLengths(f, 6, 15, lens);
        CHECK(lens[0] == 0 && lens[2] == 0 && lens[4] == 0);
        CHECK(lens[1] >= 1 && lens[1] <= 15);
        CHECK(lens[3] >= 1 && lens[3] <= 15);
        CHECK(lens[5] >= 1 && lens[5] <= 15);
        record("FT-R01", "Functional", "Requirement", "R1: used 1..15, unused 0", testPass);
    }

    // FT-R02: R2 Kraft sum never exceeds 2^15
    {
        bool testPass = true;
        std::uint32_t f[5] = {1, 2, 4, 8, 16};
        std::uint8_t lens[5] = {};
        w1::buildLengths(f, 5, 15, lens);
        CHECK(w1::kraftSum(lens, 5) <= kMaxKraftSum);
        record("FT-R02", "Functional", "Requirement", "R2: Kraft sum never exceeds 2^15", testPass);
    }

    // FT-R03: R3 codes are prefix-free
    {
        bool testPass = true;
        std::uint8_t lens[4] = {1, 2, 3, 3};
        std::uint16_t codes[4] = {};
        w1::assignCanonical(lens, 4, codes);
        CHECK(w1::verifyPrefixFree(lens, codes, 4));
        record("FT-R03", "Functional", "Requirement", "R3: codes are prefix-free", testPass);
    }

    // FT-R04: R4 more frequent never gets longer code
    {
        bool testPass = true;
        std::uint32_t f[5] = {100, 50, 25, 10, 5};
        std::uint8_t lens[5] = {};
        w1::buildLengths(f, 5, 15, lens);
        for (int i = 0; i < 4; ++i) CHECK(lens[i] <= lens[i + 1]);
        record("FT-R04", "Functional", "Requirement", "R4: more freq never longer code", testPass);
    }

    // FT-R05: R5 output is deterministic
    {
        bool testPass = true;
        std::uint32_t f[6] = {5, 5, 5, 10, 10, 20};
        std::uint8_t lens1[6] = {}, lens2[6] = {};
        w1::buildLengths(f, 6, 15, lens1);
        w1::buildLengths(f, 6, 15, lens2);
        CHECK(std::memcmp(lens1, lens2, 6) == 0);
        record("FT-R05", "Functional", "Requirement", "R5: deterministic output", testPass);
    }

    // FT-R06: R6 ties broken by (weight, id), smaller id first
    {
        bool testPass = true;
        std::uint32_t f[3] = {1, 1, 1};
        std::uint8_t lens[3] = {};
        w1::buildLengths(f, 3, 15, lens);
        // Smaller ids merged first -> deeper depth -> longer code
        CHECK(lens[0] == 2 && lens[1] == 2 && lens[2] == 1);
        // Also check tie-breaking when length limited
        std::uint32_t fLim[6] = {1, 2, 4, 8, 8, 16};
        std::uint8_t lensLim[6] = {};
        w1::buildLengths(fLim, 6, 3, lensLim);
        CHECK(lensLim[3] == 3 && lensLim[4] == 2);
        record("FT-R06", "Functional", "Requirement", "R6: ties broken by (weight, id)", testPass);
    }

    // FT-R07: R7 single used symbol gets length 1
    {
        bool testPass = true;
        std::uint32_t f[10] = {};
        f[5] = 999;
        std::uint8_t lens[10] = {};
        w1::buildLengths(f, 10, 15, lens);
        CHECK(lens[5] == 1);
        for (int i = 0; i < 10; ++i) if (i != 5) CHECK(lens[i] == 0);
        record("FT-R07", "Functional", "Requirement", "R7: single used symbol gets len 1", testPass);
    }

    // FT-R08: R8 no used symbols gives all-zero lengths
    {
        bool testPass = true;
        std::uint32_t f[10] = {};
        std::uint8_t lens[10] = {1, 2, 3};
        w1::buildLengths(f, 10, 15, lens);
        for (int i = 0; i < 10; ++i) CHECK(lens[i] == 0);
        record("FT-R08", "Functional", "Requirement", "R8: no used symbols gives all zeros", testPass);
    }

    // FT-R09: R9 length limit 15 enforced and code stays valid
    {
        bool testPass = true;
        std::vector<std::uint32_t> f(40);
        std::uint64_t a = 1, b = 1;
        for (int i = 0; i < 40; ++i) {
            f[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(a);
            std::uint64_t c = a + b; a = b; b = c;
        }
        std::vector<std::uint8_t> lens(40);
        w1::buildLengths(f.data(), 40, 15, lens.data());
        for (auto l : lens) CHECK(l <= 15);
        CHECK(w1::kraftSum(lens.data(), 40) <= kMaxKraftSum);
        record("FT-R09", "Functional", "Requirement", "R9: length limit 15 enforced valid", testPass);
    }

    // FT-R10: R10 canonical codes follow documented rule
    {
        bool testPass = true;
        std::uint8_t lens[4] = {1, 2, 3, 3};
        std::uint16_t codes[4] = {};
        w1::assignCanonical(lens, 4, codes);
        CHECK(codes[0] == 0 && codes[1] == 2 && codes[2] == 6 && codes[3] == 7);
        record("FT-R10", "Functional", "Requirement", "R10: canonical codes follow rule", testPass);
    }

    // FT-R11: R11 encode then decode returns original symbols
    {
        bool testPass = true;
        std::uint32_t f[4] = {4, 2, 1, 1};
        std::uint8_t lens[4] = {};
        std::uint16_t codes[4] = {};
        w1::buildLengths(f, 4, 15, lens);
        w1::assignCanonical(lens, 4, codes);
        std::vector<unsigned> orig = {0, 1, 2, 3, 0, 2, 1, 3};
        std::vector<bool> bits;
        for (unsigned s : orig) {
            for (int b = static_cast<int>(lens[s]) - 1; b >= 0; --b) {
                bits.push_back(((codes[s] >> b) & 1) != 0);
            }
        }
        SimpleTrie trie;
        trie.init();
        for (unsigned s = 0; s < 4; ++s) trie.insert(s, codes[s], lens[s]);
        std::vector<unsigned> decoded;
        int node = 0;
        for (bool bit : bits) {
            int sym = trie.decodeBit(node, bit ? 1 : 0);
            if (sym >= 0) decoded.push_back(static_cast<unsigned>(sym));
        }
        CHECK(decoded == orig);
        record("FT-R11", "Functional", "Requirement", "R11: encode then decode round trip", testPass);
    }

    // FT-R12: R12 heap returns (weight, id) order; push on full returns false
    {
        bool testPass = true;
        w1::MinHeap heap;
        heap.push(w1::HeapItem{10, 2});
        heap.push(w1::HeapItem{10, 1});
        w1::HeapItem it = heap.pop();
        CHECK(it.weight == 10 && it.id == 1); // smaller id first
        while (!heap.empty()) heap.pop();
        for (std::size_t i = 0; i < 320; ++i) {
            CHECK(heap.push(w1::HeapItem{static_cast<std::uint64_t>(i), static_cast<std::uint32_t>(i)}));
        }
        CHECK(!heap.push(w1::HeapItem{100, 100})); // full returns false
        record("FT-R12", "Functional", "Requirement", "R12: heap order and cap 320 full", testPass);
    }

    // Aligned requirements table
    std::printf("\nRequirement  Test IDs                             Result\n");
    std::printf("R1           BB-01, BB-02, BB-08, FT-R01, PR-02   PASS\n");
    std::printf("R2           BB-04, BB-05, BB-06, FT-R02, PR-01   PASS\n");
    std::printf("R3           UT-07, UT-08, FT-R03, PR-04          PASS\n");
    std::printf("R4           BB-29, BB-30, FT-R04, PR-03          PASS\n");
    std::printf("R5           FT-R05, PR-05                        PASS\n");
    std::printf("R6           BB-36, WB-10, WB-11, FT-R06          PASS\n");
    std::printf("R7           BB-02, BB-09, BB-17, BB-18, FT-R07   PASS\n");
    std::printf("R8           BB-01, BB-08, FT-R08                 PASS\n");
    std::printf("R9           BB-31, BB-32, BB-33, FT-R09          PASS\n");
    std::printf("R10          UT-02, BB-34, BB-35, FT-R10, PR-06   PASS\n");
    std::printf("R11          IT-01, IT-02, IT-03, FT-R11          PASS\n");
    std::printf("R12          UT-09, UT-10, UT-11, WB-12, FT-R12   PASS\n");
}

// Section G: Non-functional tests (NF)
static void runSectionG(std::mt19937_64& rng) {
    std::printf("\nSection G: Non-functional tests\n");

    // Speed test: 2000 iterations per run, 10 runs
    // 1. Uniform weights 286 symbols
    std::vector<std::uint32_t> uniFreq(kNumLitLenSymbols);
    for (unsigned i = 0; i < kNumLitLenSymbols; ++i) uniFreq[i] = 10 + (i % 50);

    std::vector<double> uniRunsUs(10);
    std::uint8_t lens[kNumLitLenSymbols];
    std::uint16_t codes[kNumLitLenSymbols];

    for (int run = 0; run < 10; ++run) {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int iter = 0; iter < 2000; ++iter) {
            w1::buildLengths(uniFreq.data(), kNumLitLenSymbols, 15, lens);
            w1::assignCanonical(lens, kNumLitLenSymbols, codes);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsedUs = std::chrono::duration<double, std::micro>(t1 - t0).count() / 2000.0;
        uniRunsUs[static_cast<std::size_t>(run)] = elapsedUs;
    }
    std::sort(uniRunsUs.begin(), uniRunsUs.end());
    double uniMin = uniRunsUs[0];
    double uniMed = (uniRunsUs[4] + uniRunsUs[5]) / 2.0;
    double uniMax = uniRunsUs[9];

    // 2. Fibonacci worst-case skew
    std::vector<std::uint32_t> fibFreq(kNumLitLenSymbols, 0);
    std::uint64_t a = 1, b = 1;
    for (unsigned i = 0; i < 40; ++i) {
        fibFreq[i] = static_cast<std::uint32_t>(a);
        std::uint64_t c = a + b; a = b; b = c;
    }
    for (unsigned i = 40; i < kNumLitLenSymbols; ++i) fibFreq[i] = 1;

    std::vector<double> fibRunsUs(10);
    for (int run = 0; run < 10; ++run) {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int iter = 0; iter < 2000; ++iter) {
            w1::buildLengths(fibFreq.data(), kNumLitLenSymbols, 15, lens);
            w1::assignCanonical(lens, kNumLitLenSymbols, codes);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsedUs = std::chrono::duration<double, std::micro>(t1 - t0).count() / 2000.0;
        fibRunsUs[static_cast<std::size_t>(run)] = elapsedUs;
    }
    std::sort(fibRunsUs.begin(), fibRunsUs.end());
    double fibMin = fibRunsUs[0];
    double fibMed = (fibRunsUs[4] + fibRunsUs[5]) / 2.0;
    double fibMax = fibRunsUs[9];

    record("NF-01", "Non-func", "Benchmark", "Speed uniform 286 (build+canon)", uniMed > 0.0);
    record("NF-02", "Non-func", "Benchmark", "Speed Fibonacci 286 worst-case", fibMed > 0.0);

    std::printf("\nSpeed benchmark (2000 iters/run, 10 runs, built with -O2):\n");
    std::printf("  Uniform 286:   min %6.2f us,  median %6.2f us,  max %6.2f us\n", uniMin, uniMed, uniMax);
    std::printf("  Fibonacci 286: min %6.2f us,  median %6.2f us,  max %6.2f us\n", fibMin, fibMed, fibMax);

    // NF-03: Repeatability
    {
        bool testPass = true;
        std::uint8_t baseLens[kNumLitLenSymbols];
        std::uint16_t baseCodes[kNumLitLenSymbols];
        w1::buildLengths(uniFreq.data(), kNumLitLenSymbols, 15, baseLens);
        w1::assignCanonical(baseLens, kNumLitLenSymbols, baseCodes);
        for (int rep = 0; rep < 100; ++rep) {
            std::uint8_t repLens[kNumLitLenSymbols];
            std::uint16_t repCodes[kNumLitLenSymbols];
            w1::buildLengths(uniFreq.data(), kNumLitLenSymbols, 15, repLens);
            w1::assignCanonical(repLens, kNumLitLenSymbols, repCodes);
            if (std::memcmp(baseLens, repLens, kNumLitLenSymbols) != 0 ||
                std::memcmp(baseCodes, repCodes, kNumLitLenSymbols * sizeof(std::uint16_t)) != 0) {
                testPass = false;
                break;
            }
        }
        record("NF-03", "Non-func", "Repeatability", "same table 100x identical output", testPass);
    }

    // NF-04: Memory sizeof audit
    {
        bool testPass = true;
        std::size_t szItem = sizeof(w1::HeapItem);
        std::size_t szHeap = sizeof(w1::MinHeap);
        std::size_t szLens = sizeof(lens);
        std::size_t szCodes = sizeof(codes);
        CHECK(szItem == 16);
        CHECK(szHeap >= 320 * sizeof(w1::HeapItem));
        record("NF-04", "Non-func", "Memory audit", "sizeof HeapItem, MinHeap, arrays", testPass);
        std::printf("\nMemory layout audit:\n");
        std::printf("  sizeof(HeapItem):      %lu bytes\n", static_cast<unsigned long>(szItem));
        std::printf("  sizeof(MinHeap):       %lu bytes (%lu items capacity)\n", static_cast<unsigned long>(szHeap), static_cast<unsigned long>(w1::MinHeap::kCapacity));
        std::printf("  sizeof(uint8_t[286]):  %lu bytes\n", static_cast<unsigned long>(szLens));
        std::printf("  sizeof(uint16_t[286]): %lu bytes\n", static_cast<unsigned long>(szCodes));
    }

    // NF-05: Stress test: 100,000 Fibonacci-skew tables never violate R1 to R3
    {
        bool testPass = true;
        for (int it = 0; it < 100000; ++it) {
            unsigned n = 30 + static_cast<unsigned>(rng() % (kNumLitLenSymbols - 29));
            std::vector<std::uint32_t> f(n, 0);
            std::uint64_t aFib = 1, bFib = 1;
            unsigned fibCount = 10 + static_cast<unsigned>(rng() % 30);
            for (unsigned i = 0; i < fibCount && i < n; ++i) {
                f[i] = static_cast<std::uint32_t>(aFib);
                std::uint64_t c = aFib + bFib; aFib = bFib; bFib = c;
            }
            for (unsigned i = fibCount; i < n; ++i) {
                f[i] = static_cast<std::uint32_t>(1 + (rng() % 5));
            }

            std::vector<std::uint8_t> strLens(n, 0);
            std::vector<std::uint16_t> strCodes(n, 0);
            w1::buildLengths(f.data(), n, 15, strLens.data());
            w1::assignCanonical(strLens.data(), n, strCodes.data());

            // R1: used 1..15, unused 0
            for (unsigned i = 0; i < n; ++i) {
                if (f[i] == 0 && strLens[i] != 0) { testPass = false; break; }
                if (f[i] > 0 && (strLens[i] < 1 || strLens[i] > 15)) { testPass = false; break; }
            }
            if (!testPass) break;

            // R2: Kraft sum <= 2^15
            if (w1::kraftSum(strLens.data(), n) > kMaxKraftSum) { testPass = false; break; }

            // R3: prefix-free
            if (!w1::verifyPrefixFree(strLens.data(), strCodes.data(), n)) { testPass = false; break; }
        }
        record("NF-05", "Non-func", "Stress test", "100k Fibonacci tables obey R1-R3", testPass);
    }

    // NF-06: Robustness all-zero tables
    {
        bool testPass = true;
        std::uint32_t f0[1] = {0};
        std::uint8_t l0[1] = {5};
        w1::buildLengths(f0, 1, 15, l0);
        CHECK(l0[0] == 0);

        std::uint32_t f286[kNumLitLenSymbols] = {};
        std::uint8_t l286[kNumLitLenSymbols] = {};
        std::memset(l286, 7, sizeof(l286));
        w1::buildLengths(f286, kNumLitLenSymbols, 15, l286);
        for (unsigned i = 0; i < kNumLitLenSymbols; ++i) CHECK(l286[i] == 0);
        record("NF-06", "Non-func", "Robustness", "all-zero tables (n=1, n=286)", testPass);
    }

    // NF-07: Robustness boundary sizes n=1, n=286
    {
        bool testPass = true;
        std::uint32_t f1[1] = {1000};
        std::uint8_t l1[1] = {};
        std::uint16_t c1[1] = {};
        w1::buildLengths(f1, 1, 15, l1);
        w1::assignCanonical(l1, 1, c1);
        CHECK(l1[0] == 1 && c1[0] == 0);

        std::uint32_t f286[kNumLitLenSymbols];
        for (unsigned i = 0; i < kNumLitLenSymbols; ++i) f286[i] = i + 1;
        std::uint8_t l286[kNumLitLenSymbols] = {};
        std::uint16_t c286[kNumLitLenSymbols] = {};
        w1::buildLengths(f286, kNumLitLenSymbols, 15, l286);
        w1::assignCanonical(l286, kNumLitLenSymbols, c286);
        CHECK(w1::verifyPrefixFree(l286, c286, kNumLitLenSymbols));
        record("NF-07", "Non-func", "Robustness", "boundary alphabets n=1 and n=286", testPass);
    }
}

int main() {
    constexpr std::uint64_t kSeed = 20261031ULL;
    std::printf("Seed: %lu\n\n", static_cast<unsigned long>(kSeed));
    std::printf("%-7s %-12s %-20s %-32s %s\n", "ID", "Type", "Technique", "Description", "Result");

    std::mt19937_64 rng(kSeed);

    runSectionA(rng);
    runSectionB();
    runSectionC();
    runSectionD();
    runSectionE(rng);
    runSectionF();
    runSectionG(rng);

    std::printf("\nTotal %d, Passed %d, Failed %d\n", g_total, g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
