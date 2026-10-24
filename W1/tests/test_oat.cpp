// test_oat.cpp - black-box tests for W1 (Step 3).
//
//  1. Orthogonal array testing (OAT): 4 block "factors" x 3 levels each would need 81 tests
//     if every combination were tried. An L9 array covers every PAIR of levels in 9 tests.
//  2. Boundary value analysis (BVA) on the output buffer size.
//
// Black-box means: these tests only use W1's public API and never look inside it.
#include <cstdio>
#include <random>
#include <vector>

#include "huffman.h"
#include "test_util.h"

namespace {

constexpr int kRows = 9;
constexpr int kCols = 4;

// L9(3^4): rows are tests, columns are factors, entries are levels 1..3.
//   A = literal distribution : 1 uniform bytes   2 skewed (mostly 'e')   3 one repeated byte
//   B = match length         : 1 always 3 (min)  2 random 3..258         3 always 258 (max)
//   C = match distance       : 1 always 1 (min)  2 random 1..32768       3 always 32768 (max)
//   D = block size           : 1 a few tokens    2 about 5000 words      3 completely full
const int L9[kRows][kCols] = {
    {1, 1, 1, 1}, {1, 2, 2, 2}, {1, 3, 3, 3},
    {2, 1, 2, 3}, {2, 2, 3, 1}, {2, 3, 1, 2},
    {3, 1, 3, 2}, {3, 2, 1, 3}, {3, 3, 2, 1},
};

// Builds a block for one combination of levels. Matches and literal-only tokens alternate.
std::unique_ptr<cmp::TokenBlock> buildBlock(int a, int b, int c, int d, unsigned seed) {
    std::mt19937 rng(seed);
    auto tb = newBlock();
    auto lit = [&]() -> std::uint8_t {
        switch (a) {
            case 1: return static_cast<std::uint8_t>(rng());
            case 2: return (rng() % 10 < 8) ? static_cast<std::uint8_t>('e')
                                            : static_cast<std::uint8_t>('a' + rng() % 20);
            default: return static_cast<std::uint8_t>('z');
        }
    };
    auto len = [&]() -> std::uint32_t {
        switch (b) {
            case 1: return 3;
            case 2: return 3 + rng() % 256;
            default: return 258;
        }
    };
    auto dist = [&]() -> std::uint32_t {
        switch (c) {
            case 1: return 1;
            case 2: return 1 + rng() % 32768;
            default: return 32768;
        }
    };
    const unsigned target = d == 1 ? 4 : d == 2 ? 5000 : cmp::kBlockTokens;
    unsigned i = 0;
    while (tb->n < target && tb->hasRoomForToken()) {
        if (i++ % 2 == 0) {
            const std::uint32_t dd = dist();            // evaluated in a fixed order on every compiler
            const std::uint32_t ll = len();
            tb->addToken(cmp::Token{dd, ll, lit()});
        } else {
            tb->addToken(cmp::Token{0, 0, lit()});
        }
    }
    while (d == 3 && !tb->full()) tb->addLiteral(lit());   // use the last word if one is left
    return tb;
}

void runCombination(int a, int b, int c, int d, int row) {
    const int before = failureCount();
    auto tb = buildBlock(a, b, c, d, 1000u + static_cast<unsigned>(a * 27 + b * 9 + c * 3 + d));
    std::vector<cmp::Tok> out;
    w1::Plan p;
    const bool ok = roundTrip(*tb, out, &p);              // plan -> emit -> readTables -> decodeBlock
    CHECK(ok);
    if (ok) {
        bool same = out.size() == tb->n;
        for (std::uint32_t i = 0; same && i < tb->n; ++i) same = out[i] == tb->tok[i];
        CHECK(same);                                      // lossless
        CHECK(w1::verifyPrefixFree(p));                   // valid codes
        bool within15 = true;
        for (unsigned i = 0; i < cmp::kNumLitLen; ++i) within15 = within15 && p.litLen[i] <= 15;
        for (unsigned i = 0; i < cmp::kNumDist; ++i) within15 = within15 && p.distLen[i] <= 15;
        CHECK(within15);                                  // every length fits the 4-bit header field
        if (d == 3) CHECK(tb->n == cmp::kBlockTokens);    // the "full" level really is full
    }
    if (failureCount() != before) {
        std::fprintf(stderr, "  ^ failure(s) above came from combination row %d (A%d B%d C%d D%d)\n",
                     row, a, b, c, d);
    }
}

}  // namespace

// The array is only useful if it really is orthogonal: for every pair of columns, each of the
// 9 level pairs must appear exactly once.
static void testArrayIsOrthogonal() {
    for (int c1 = 0; c1 < kCols; ++c1) {
        for (int c2 = c1 + 1; c2 < kCols; ++c2) {
            int seen[4][4] = {};
            for (int r = 0; r < kRows; ++r) ++seen[L9[r][c1]][L9[r][c2]];
            for (int x = 1; x <= 3; ++x) {
                for (int y = 1; y <= 3; ++y) CHECK(seen[x][y] == 1);
            }
        }
    }
}

static void testOrthogonalArrayRows() {
    for (int r = 0; r < kRows; ++r) runCombination(L9[r][0], L9[r][1], L9[r][2], L9[r][3], r + 1);
}

// For comparison only: all 3^4 = 81 combinations. The 9 rows above are a subset of these.
// With more factors this becomes impossible, which is the reason OAT exists.
static void testAllCombinationsForComparison() {
    int row = 0;
    for (int a = 1; a <= 3; ++a)
        for (int b = 1; b <= 3; ++b)
            for (int c = 1; c <= 3; ++c)
                for (int d = 1; d <= 3; ++d) runCombination(a, b, c, d, ++row);
}

// ---------- BVA on the output buffer size ----------------------------------------------
static void testOutputBufferBoundary() {
    auto tb = newBlock();
    tb->addLiteral('a'); tb->addLiteral('b'); tb->addLiteral('c');
    tb->addMatch(9, 3);
    tb->addLiteral('x');
    const w1::Plan p = w1::plan(*tb);
    const std::size_t need = p.totalBytes();                  // 161

    {   // exactly enough: everything succeeds
        std::vector<std::uint8_t> buf(need);
        w1::BitWriter w(buf.data(), buf.size());
        CHECK(w1::emit(*tb, p, w) == w1::Status::Ok);
        CHECK(w.finish() == need);
        CHECK(!w.overflow());
    }
    {   // one byte short: the problem must be detectable, by emit() or by overflow() afterwards.
        // (emit() only sees full 32-bit words being written; the last partial byte is written
        //  by finish(), so for a buffer that is short by 1-3 bytes the failure shows up there.)
        std::vector<std::uint8_t> buf(need - 1);
        w1::BitWriter w(buf.data(), buf.size());
        const w1::Status s = w1::emit(*tb, p, w);
        w.finish();
        CHECK(s == w1::Status::BufferOverflow || w.overflow());
    }
    {   // half the size: emit() itself reports it
        std::vector<std::uint8_t> buf(need / 2);
        w1::BitWriter w(buf.data(), buf.size());
        CHECK(w1::emit(*tb, p, w) == w1::Status::BufferOverflow);
    }
    {   // no buffer at all
        w1::BitWriter w(nullptr, 0);
        CHECK(w1::emit(*tb, p, w) == w1::Status::BufferOverflow);
    }
}

int main() {
    testArrayIsOrthogonal();
    testOrthogonalArrayRows();
    testAllCombinationsForComparison();
    testOutputBufferBoundary();
    return finishTests("test_oat");
}
