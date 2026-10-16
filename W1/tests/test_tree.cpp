// Tests for the min-heap, Huffman lengths, length limiting, canonical codes, prefix-free
// verification, and agreement between the trie walk and the table decoder.
#include <algorithm>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <vector>

#include "huffman.h"
#include "huffman_tree.h"
#include "min_heap.h"
#include "test_util.h"

static void testMinHeap() {
    std::mt19937 rng(1);
    for (int round = 0; round < 200; ++round) {
        w1::MinHeap heap;
        std::vector<w1::HeapItem> items;
        const unsigned n = 1 + rng() % 300;
        for (unsigned i = 0; i < n; ++i) {
            w1::HeapItem it{rng() % 20, i};                 // many equal weights on purpose
            items.push_back(it);
            CHECK(heap.push(it));
        }
        std::sort(items.begin(), items.end(), w1::heapLess);
        for (const auto& expected : items) {
            const w1::HeapItem got = heap.pop();
            CHECK(got.weight == expected.weight && got.id == expected.id);   // ties break by id
        }
        CHECK(heap.empty());
    }
}

// Reference Huffman cost by the plain O(n^2) algorithm, for comparison.
static std::uint64_t referenceCost(std::vector<std::uint64_t> w) {
    std::uint64_t cost = 0;
    while (w.size() > 1) {
        std::sort(w.begin(), w.end());
        const std::uint64_t s = w[0] + w[1];
        cost += s;
        w.erase(w.begin(), w.begin() + 2);
        w.push_back(s);
    }
    return cost;
}

static std::uint64_t codedCost(const std::vector<std::uint32_t>& f, const std::vector<std::uint8_t>& lens) {
    std::uint64_t c = 0;
    for (std::size_t i = 0; i < f.size(); ++i) c += static_cast<std::uint64_t>(f[i]) * lens[i];
    return c;
}

static void testClassicExample() {
    // textbook frequencies 5 9 12 13 16 45 -> lengths 4 4 3 3 3 1, total cost 224
    const std::vector<std::uint32_t> f = {5, 9, 12, 13, 16, 45};
    std::vector<std::uint8_t> lens(f.size());
    w1::buildLengths(f.data(), static_cast<unsigned>(f.size()), 15, lens.data());
    const std::vector<std::uint8_t> expected = {4, 4, 3, 3, 3, 1};
    CHECK(lens == expected);
    CHECK(codedCost(f, lens) == 224);
    CHECK(w1::kraftSum(lens.data(), 6) == (1u << 15));       // complete code
}

static void testOptimalityAndKraft() {
    std::mt19937 rng(5);
    for (int round = 0; round < 500; ++round) {
        const unsigned n = 2 + rng() % 60;
        std::vector<std::uint32_t> f(n);
        for (auto& x : f) x = (rng() % 5 == 0) ? 0 : 1 + rng() % 1000;
        std::vector<std::uint64_t> used;
        for (auto x : f) if (x) used.push_back(x);
        if (used.size() < 2) continue;

        std::vector<std::uint8_t> lens(n);
        w1::buildLengths(f.data(), n, 15, lens.data());
        for (unsigned i = 0; i < n; ++i) CHECK((f[i] == 0) == (lens[i] == 0));
        CHECK(codedCost(f, lens) == referenceCost(used));                // Huffman is optimal
        CHECK(w1::kraftSum(lens.data(), n) == (1u << 15));               // complete
        std::vector<std::uint16_t> codes(n);
        w1::assignCanonical(lens.data(), n, codes.data());
        CHECK(w1::verifyPrefixFree(lens.data(), codes.data(), n));
    }
}

static void testDeterministicTieBreak() {
    // equal frequencies must always give the same lengths: the README golden example
    std::vector<std::uint32_t> f(286, 0);
    for (unsigned s : {97u, 98u, 99u, 120u, 256u, 263u}) f[s] = 1;
    std::vector<std::uint8_t> lens(286);
    w1::buildLengths(f.data(), 286, 15, lens.data());
    CHECK(lens[256] == 2 && lens[263] == 2);
    CHECK(lens[97] == 3 && lens[98] == 3 && lens[99] == 3 && lens[120] == 3);
    std::vector<std::uint16_t> codes(286);
    w1::assignCanonical(lens.data(), 286, codes.data());
    CHECK(codes[256] == 0b00 && codes[263] == 0b01);
    CHECK(codes[97] == 0b100 && codes[98] == 0b101 && codes[99] == 0b110 && codes[120] == 0b111);
}

static void testSingleAndEmpty() {
    std::vector<std::uint32_t> f(30, 0);
    std::vector<std::uint8_t> lens(30, 9);
    w1::buildLengths(f.data(), 30, 15, lens.data());
    for (auto l : lens) CHECK(l == 0);                                   // no symbols, no codes

    f[7] = 12345;
    w1::buildLengths(f.data(), 30, 15, lens.data());
    CHECK(lens[7] == 1);                                                 // single symbol: 1-bit code
    for (unsigned i = 0; i < 30; ++i) if (i != 7) CHECK(lens[i] == 0);
    std::vector<std::uint16_t> codes(30);
    w1::assignCanonical(lens.data(), 30, codes.data());
    CHECK(codes[7] == 0);
}

static void testLengthLimiting() {
    // Fibonacci frequencies make the natural tree 39 deep; the limit must cap it at 15.
    std::vector<std::uint32_t> f;
    std::uint64_t a = 1, b = 1;
    for (int i = 0; i < 40; ++i) {
        f.push_back(static_cast<std::uint32_t>(a));
        const std::uint64_t c = a + b;
        a = b;
        b = c;
    }
    const unsigned n = static_cast<unsigned>(f.size());
    std::vector<std::uint8_t> unlimited(n), limited(n);
    w1::buildLengths(f.data(), n, 15, limited.data());
    // an "unlimited" reference: ask for a bigger limit than 15 is clamped, so compute depth by cost
    std::vector<std::uint64_t> w(f.begin(), f.end());
    const std::uint64_t optimal = referenceCost(w);

    CHECK(*std::max_element(limited.begin(), limited.end()) <= 15);
    CHECK(w1::kraftSum(limited.data(), n) <= (1u << 15));
    CHECK(codedCost(f, limited) >= optimal);                             // never better than optimal
    std::vector<std::uint16_t> codes(n);
    w1::assignCanonical(limited.data(), n, codes.data());
    CHECK(w1::verifyPrefixFree(limited.data(), codes.data(), n));

    // random skewed alphabets with the full 286 symbols
    std::mt19937 rng(11);
    for (int round = 0; round < 200; ++round) {
        std::vector<std::uint32_t> g(286);
        double scale = 1;
        for (auto& x : g) {
            x = static_cast<std::uint32_t>(1 + scale);
            scale *= 1.0 + (rng() % 100) / 50.0;
            if (scale > 1e8) scale = 1e8;
        }
        std::shuffle(g.begin(), g.end(), rng);
        std::vector<std::uint8_t> lens(286);
        w1::buildLengths(g.data(), 286, 15, lens.data());
        CHECK(*std::max_element(lens.begin(), lens.end()) <= 15);
        CHECK(w1::kraftSum(lens.data(), 286) <= (1u << 15));
        std::vector<std::uint16_t> cs(286);
        w1::assignCanonical(lens.data(), 286, cs.data());
        CHECK(w1::verifyPrefixFree(lens.data(), cs.data(), 286));
        for (auto l : lens) CHECK(l >= 1);                               // all symbols used -> all coded
    }

    // a small limit on a normal alphabet
    std::vector<std::uint32_t> h(100);
    for (unsigned i = 0; i < 100; ++i) h[i] = 1 + i * i * i;
    std::vector<std::uint8_t> lens7(100);
    w1::buildLengths(h.data(), 100, 7, lens7.data());
    CHECK(*std::max_element(lens7.begin(), lens7.end()) <= 7);
    CHECK(w1::kraftSum(lens7.data(), 100) <= (1u << 15));
}

static void testPrefixFreeVerifier() {
    // over-subscribed: three 1-bit codes
    const std::uint8_t bad[3] = {1, 1, 1};
    std::uint16_t codes[3];
    w1::assignCanonical(bad, 3, codes);
    CHECK(!w1::verifyPrefixFree(bad, codes, 3));

    // a hand-made code where one code is a prefix of another: 0, 01
    const std::uint8_t lensP[2] = {1, 2};
    const std::uint16_t codesP[2] = {0b0, 0b01};
    CHECK(!w1::verifyPrefixFree(lensP, codesP, 2));

    // duplicate codes
    const std::uint8_t lensD[2] = {2, 2};
    const std::uint16_t codesD[2] = {0b10, 0b10};
    CHECK(!w1::verifyPrefixFree(lensD, codesD, 2));

    // incomplete but valid (a single 1-bit code)
    const std::uint8_t lensI[1] = {1};
    const std::uint16_t codesI[1] = {0};
    CHECK(w1::verifyPrefixFree(lensI, codesI, 1));
}

// The production table decoder and the reference trie walk must agree on every bit stream.
static void testTrieAgreesWithTable() {
    std::mt19937 rng(23);
    for (int round = 0; round < 100; ++round) {
        const unsigned n = 2 + rng() % 285;
        std::vector<std::uint32_t> f(n);
        for (auto& x : f) x = (rng() % 4 == 0) ? 0 : 1 + (rng() % 2 ? rng() % 10 : rng() % 100000);
        std::vector<std::uint8_t> lens(n);
        w1::buildLengths(f.data(), n, 15, lens.data());
        if (std::count(lens.begin(), lens.end(), 0) == static_cast<long>(n)) continue;

        std::vector<std::uint16_t> codes(n);
        w1::assignCanonical(lens.data(), n, codes.data());
        w1::CodeTrie trie;
        CHECK(w1::buildTrie(lens.data(), codes.data(), n, trie));
        std::unique_ptr<w1::DecodeTable> table(new w1::DecodeTable);
        CHECK(w1::buildDecodeTable(lens.data(), n, *table) == w1::Status::Ok);

        // encode a random symbol sequence, then decode it both ways
        std::vector<unsigned> used;
        for (unsigned i = 0; i < n; ++i) if (lens[i]) used.push_back(i);
        std::vector<unsigned> seq;
        std::uint64_t bits = 0;
        for (int i = 0; i < 400; ++i) {
            seq.push_back(used[rng() % used.size()]);
            bits += lens[seq.back()];
        }
        std::vector<std::uint8_t> buf(static_cast<std::size_t>((bits + 7) / 8) + 8);
        w1::BitWriter w(buf.data(), buf.size());
        for (unsigned s : seq) w.write(codes[s], lens[s]);
        const std::size_t bytes = w.finish();

        w1::BitReader viaTrie(buf.data(), bytes, bits), viaTable(buf.data(), bytes, bits);
        for (unsigned expected : seq) {
            const int t = w1::trieDecode(trie, viaTrie);
            viaTable.refill();
            std::uint32_t s = 0;
            const w1::Status st = w1::decodeSymbol(*table, viaTable, s);
            CHECK(st == w1::Status::Ok);
            CHECK(t == static_cast<int>(expected));
            CHECK(s == expected);
        }
        CHECK(viaTrie.bitsLeft() == 0 && viaTable.bitsLeft() == 0);
    }
}

int main() {
    testMinHeap();
    testClassicExample();
    testOptimalityAndKraft();
    testDeterministicTieBreak();
    testSingleAndEmpty();
    testLengthLimiting();
    testPrefixFreeVerifier();
    testTrieAgreesWithTable();
    return finishTests("test_tree");
}