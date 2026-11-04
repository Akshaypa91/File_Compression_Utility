// test_symbols.cpp - unit, white-box and black-box tests for shared/include/symbols.hpp.
//
//   Compile-time: constants and a few known values are checked with static_assert, so the
//                 build fails if someone edits the shared contract carelessly.
//   Black-box:    exhaustive checks (every literal, every length, every distance, and every
//                 length x distance pair) plus an independent rule-based oracle for the tables.
//   White-box:    both sides of every branch: distSymbol's ternary, addToken's
//                 literal/match split, hasRoomForToken/full boundaries, and the table
//                 constructor, which normally runs only at compile time, is run here at
//                 runtime so coverage tools can see it.
//
// Not tested on purpose: addMatch() with a length outside 3..258 or a distance outside
// 1..32768. Those are documented preconditions; breaking them indexes outside a table.
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "symbols.hpp"
#include "test_util.h"

using namespace cmp;

// ---------- compile-time contracts --------------------------------------------------------
static_assert(kNumLitLen == 286 && kNumDist == 30 && kNumLenCodes == 29, "DEFLATE alphabet sizes");
static_assert(kEob == 256 && kFirstLenSym == kEob + 1, "length symbols follow EOB");
static_assert(kFirstLenSym + kNumLenCodes == kNumLitLen, "257 + 29 = 286: alphabets line up");
static_assert(kMinMatch == 3 && kMaxMatch == 258 && kMaxDistance == 32768, "match limits");
static_assert(kMaxMatch - kMinMatch <= 0xFF, "length-3 fits the 8-bit field of a match word");
static_assert(kMaxDistance - 1 <= 0xFFFF, "distance-1 fits the 16-bit field of a match word");
static_assert(kMatchFlag == 0x80000000u, "bit 31 marks a match");
static_assert(kMaxCodeLen == 15 && kBlockTokens == 32768, "code length and block size");

static_assert(mkMatch(9, 3) == 0x80060002u, "worked example: length 9, distance 3");
static_assert(lenSymbol(3) == 257 && lenSymbol(9) == 263 && lenSymbol(258) == 285, "length symbols at known points");
static_assert(lenSymbol(257) == 284 && lenSymbol(227) == 284, "bucket 27 holds 227..257");
static_assert(distSymbol(1) == 0 && distSymbol(3) == 2 && distSymbol(32768) == 29, "distance symbols at known points");
static_assert(distSymbol(256) == 15 && distSymbol(257) == 16, "the distLow / distHigh switch at 256");

// ---------- token words -------------------------------------------------------------------
static void testLiteralWords() {
    for (unsigned b = 0; b < 256; ++b) {
        const Tok t = mkLit(static_cast<std::uint8_t>(b));
        CHECK(!isMatch(t));
        CHECK(litOf(t) == b);
    }
}

// EXHAUSTIVE: every legal length with every legal distance (about 8.4 million pairs)
static void testMatchWordsExhaustive() {
    bool allGood = true;
    for (std::uint32_t len = kMinMatch; len <= kMaxMatch; ++len) {
        for (std::uint32_t dist = 1; dist <= kMaxDistance; ++dist) {
            const Tok t = mkMatch(len, dist);
            if (!isMatch(t) || lenOf(t) != len || distOf(t) != dist) { allGood = false; break; }
        }
        if (!allGood) break;
    }
    CHECK(allGood);
    CHECK(mkMatch(3, 1) == (kMatchFlag | 0u));                       // smallest fields are all zero
    CHECK(mkMatch(258, 32768) == (kMatchFlag | (255u << 16) | 32767u));   // largest fields
}

// ---------- the bucket tables --------------------------------------------------------------
static void testEveryLengthMapsToExactlyOneBucket() {
    unsigned previous = kFirstLenSym;
    for (std::uint32_t len = kMinMatch; len <= kMaxMatch; ++len) {
        const unsigned s = lenSymbol(len);
        CHECK(s >= kFirstLenSym && s < kNumLitLen);
        CHECK(s >= previous);                                         // buckets appear in order
        previous = s;
        const unsigned li = s - kFirstLenSym;
        CHECK(len >= kTables.lenBase[li]);
        const std::uint32_t extra = len - kTables.lenBase[li];
        CHECK(extra < (1u << kTables.lenExtra[li]));                  // the extra bits can hold the offset
        CHECK(kTables.lenBase[li] + extra == len);                    // decoder rebuilds the same length
    }
    CHECK(lenSymbol(258) == 285);                                     // 258 has its own bucket (0 extra bits)
    CHECK(kTables.lenExtra[kNumLenCodes - 1] == 0);
}

static void testEveryDistanceMapsToExactlyOneBucket() {
    unsigned previous = 0;
    for (std::uint32_t d = 1; d <= kMaxDistance; ++d) {
        const unsigned s = distSymbol(d);
        CHECK(s < kNumDist);
        CHECK(s >= previous);
        previous = s;
        CHECK(d >= kTables.distBase[s]);
        const std::uint32_t extra = d - kTables.distBase[s];
        CHECK(extra < (1u << kTables.distExtra[s]));
        CHECK(kTables.distBase[s] + extra == d);
    }
}

// An independent oracle: the extra-bit counts alone determine every base (RFC 1951 rule),
// so rebuild the bases from them and compare with the table in the header.
static void testTablesAgreeWithTheRule() {
    std::uint32_t base = kMinMatch;
    for (unsigned c = 0; c + 1 < kNumLenCodes; ++c) {
        CHECK(kTables.lenBase[c] == base);
        base += 1u << kTables.lenExtra[c];
    }
    CHECK(kTables.lenBase[kNumLenCodes - 1] == kMaxMatch);            // last bucket is the single value 258
    CHECK(base == kMaxMatch + 1);                                     // bucket 27 formula reaches 258 ...
    CHECK(lenSymbol(258) == kFirstLenSym + kNumLenCodes - 1);         // ... but 258 is given to bucket 28

    std::uint32_t d = 1;
    for (unsigned c = 0; c < kNumDist; ++c) {
        CHECK(kTables.distBase[c] == d);
        d += 1u << kTables.distExtra[c];
    }
    CHECK(d == kMaxDistance + 1);                                     // buckets cover exactly 1..32768 with no gap
}

// The table constructor is constexpr and normally runs only inside the compiler.
// Building one at runtime exercises its loops for coverage and must give identical tables.
static void testRuntimeBuiltTablesEqualCompileTimeOnes() {
    const SymbolTables rt;
    CHECK(std::memcmp(rt.lenBase, kTables.lenBase, sizeof rt.lenBase) == 0);
    CHECK(std::memcmp(rt.lenExtra, kTables.lenExtra, sizeof rt.lenExtra) == 0);
    CHECK(std::memcmp(rt.distBase, kTables.distBase, sizeof rt.distBase) == 0);
    CHECK(std::memcmp(rt.distExtra, kTables.distExtra, sizeof rt.distExtra) == 0);
    CHECK(std::memcmp(rt.lenCode, kTables.lenCode, sizeof rt.lenCode) == 0);
    CHECK(std::memcmp(rt.distLow, kTables.distLow, sizeof rt.distLow) == 0);
    CHECK(std::memcmp(rt.distHigh, kTables.distHigh, sizeof rt.distHigh) == 0);
}

// ---------- TokenBlock ---------------------------------------------------------------------
static bool isCleanAfterReset(const TokenBlock& tb) {
    if (tb.n != 0 || tb.extraBitsTotal != 0 || tb.src != nullptr || tb.srcLen != 0) return false;
    for (unsigned i = 0; i < kNumLitLen; ++i) if (tb.litlenFreq[i] != (i == kEob ? 1u : 0u)) return false;
    for (unsigned i = 0; i < kNumDist; ++i) if (tb.distFreq[i] != 0) return false;
    return true;
}

static void testResetPostconditions() {
    auto tb = newBlock();
    CHECK(isCleanAfterReset(*tb));
    const std::uint8_t dummy[1] = {0};                                 // dirty every field, then reset
    tb->addLiteral('q'); tb->addMatch(20, 100);
    tb->src = dummy; tb->srcLen = 1;
    CHECK(!isCleanAfterReset(*tb));
    tb->reset();
    CHECK(isCleanAfterReset(*tb));
}

static void testAddLiteralAndMatch() {
    auto tb = newBlock();
    tb->addLiteral('a');
    CHECK(tb->n == 1 && tb->tok[0] == mkLit('a') && tb->litlenFreq['a'] == 1);

    tb->reset();
    tb->addMatch(9, 3);                                                // worked example, no extra bits
    CHECK(tb->n == 1 && tb->tok[0] == 0x80060002u);
    CHECK(tb->litlenFreq[263] == 1 && tb->distFreq[2] == 1 && tb->extraBitsTotal == 0);

    tb->reset();
    tb->addMatch(20, 100);                                             // length bucket 12 (2 extra) + distance bucket 13 (5 extra)
    CHECK(tb->litlenFreq[269] == 1 && tb->distFreq[13] == 1 && tb->extraBitsTotal == 7);

    tb->reset();
    tb->addMatch(258, 32768);                                          // 0 + 13 extra bits
    CHECK(tb->litlenFreq[285] == 1 && tb->distFreq[29] == 1 && tb->extraBitsTotal == 13);

    tb->reset();
    tb->addMatch(257, 32768);                                          // length bucket 27 has 5 extra bits
    CHECK(tb->litlenFreq[284] == 1 && tb->extraBitsTotal == 5 + 13);

    tb->reset();
    tb->addMatch(3, 1);                                                // smallest match
    CHECK(tb->litlenFreq[257] == 1 && tb->distFreq[0] == 1 && tb->extraBitsTotal == 0);
}

// Found by mutation testing: checking only 'a' misses bugs that affect bytes >= 128.
static void testEveryLiteralCountedInItsOwnSlot() {
    auto tb = newBlock();
    for (unsigned b = 0; b < 256; ++b) {
        tb->reset();
        tb->addLiteral(static_cast<std::uint8_t>(b));
        CHECK(tb->n == 1 && tb->tok[0] == mkLit(static_cast<std::uint8_t>(b)));
        bool onlyThatSlot = true;
        for (unsigned i = 0; i < kNumLitLen; ++i) {
            const std::uint32_t expected = (i == b || i == kEob) ? 1u : 0u;
            onlyThatSlot = onlyThatSlot && tb->litlenFreq[i] == expected;
        }
        CHECK(onlyThatSlot);
    }
}

static void testAddToken() {
    auto tb = newBlock();
    tb->addToken(Token{0, 0, 'a'});                                    // literal token: one word
    CHECK(tb->n == 1 && tb->tok[0] == mkLit('a'));

    tb->addToken(Token{3, 9, 'x'});                                    // match token: match word, then literal word
    CHECK(tb->n == 3);
    CHECK(tb->tok[1] == mkMatch(9, 3) && tb->tok[2] == mkLit('x'));
    CHECK(tb->litlenFreq['a'] == 1 && tb->litlenFreq['x'] == 1 && tb->litlenFreq[263] == 1 && tb->distFreq[2] == 1);
}

// BVA on capacity: a Token may need 2 words
static void testCapacityBoundaries() {
    auto tb = newBlock();
    tb->n = kBlockTokens - 3;  CHECK(tb->hasRoomForToken() && !tb->full());   // 32765
    tb->n = kBlockTokens - 2;  CHECK(tb->hasRoomForToken() && !tb->full());   // 32766: exactly 2 words left
    tb->n = kBlockTokens - 1;  CHECK(!tb->hasRoomForToken() && !tb->full());  // 32767: only 1 word left
    tb->n = kBlockTokens;      CHECK(!tb->hasRoomForToken() && tb->full());   // 32768
    tb->n = 0;                 CHECK(tb->hasRoomForToken() && !tb->full());
}

// Property: the counters always agree with the words stored
static void testCountersMatchWords() {
    std::mt19937 rng(11);
    auto tb = newBlock();
    for (int round = 0; round < 200; ++round) {
        tb->reset();
        unsigned literals = 0, matches = 0;
        while (tb->hasRoomForToken() && tb->n < 4000) {
            if (rng() % 2) {
                const std::uint32_t off = 1 + static_cast<std::uint32_t>(rng() % 32768);
                const std::uint32_t len = 3 + static_cast<std::uint32_t>(rng() % 256);
                tb->addToken(Token{off, len, static_cast<std::uint8_t>(rng())});
                ++matches; ++literals;
            } else {
                tb->addToken(Token{0, 0, static_cast<std::uint8_t>(rng())});
                ++literals;
            }
        }
        std::uint64_t litSum = 0, lenSum = 0, distSum = 0, extra = 0;
        for (unsigned i = 0; i < 256; ++i) litSum += tb->litlenFreq[i];
        for (unsigned i = kFirstLenSym; i < kNumLitLen; ++i) lenSum += tb->litlenFreq[i];
        for (unsigned i = 0; i < kNumDist; ++i) distSum += tb->distFreq[i];
        for (std::uint32_t i = 0; i < tb->n; ++i) {
            if (!isMatch(tb->tok[i])) continue;
            const unsigned ls = lenSymbol(lenOf(tb->tok[i]));
            const unsigned ds = distSymbol(distOf(tb->tok[i]));
            extra += kTables.lenExtra[ls - kFirstLenSym] + kTables.distExtra[ds];
        }
        CHECK(litSum == literals);
        CHECK(lenSum == matches && distSum == matches);
        CHECK(tb->litlenFreq[kEob] == 1);
        CHECK(tb->extraBitsTotal == extra);
        CHECK(tb->n == literals + matches);
    }
}

int main() {
    testLiteralWords();
    testMatchWordsExhaustive();
    testEveryLengthMapsToExactlyOneBucket();
    testEveryDistanceMapsToExactlyOneBucket();
    testTablesAgreeWithTheRule();
    testRuntimeBuiltTablesEqualCompileTimeOnes();
    testResetPostconditions();
    testAddLiteralAndMatch();
    testEveryLiteralCountedInItsOwnSlot();
    testAddToken();
    testCapacityBoundaries();
    testCountersMatchWords();
    return finishTests("test_symbols");
}
