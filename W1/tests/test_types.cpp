// test_types.cpp - unit and black-box tests for huffman_types.h.
//
// The header has no branches (only two one-line functions), so "white-box" here means
// statement coverage of Plan::totalBits() and Plan::totalBytes(). Most of the value is in
// the compile-time static_asserts: they fail the BUILD if a constant is changed in a way
// that would silently break the 4-bit header, the 10-bit fast table or the entry packing.
#include <cstdint>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "huffman.h"
#include "test_util.h"

// ---------- compile-time contracts --------------------------------------------------------
static_assert(static_cast<int>(w1::Status::Ok) == 0, "Ok must be 0 so that 'if (status)' style checks work");
static_assert(static_cast<int>(w1::Status::TrailingBits) == 8, "9 statuses, values 0..8");

static_assert(w1::kHeaderBits == (cmp::kNumLitLen + cmp::kNumDist) * 4, "header = every length in 4 bits");
static_assert(w1::kHeaderBits == 1264, "316 lengths x 4 bits");
static_assert(w1::kHeaderBits % 8 == 0, "header is a whole number of bytes (158)");
static_assert(cmp::kMaxCodeLen <= 15, "a code length must fit the 4-bit header field");

static_assert(w1::kFastBits == 10, "design: 10-bit fast table");
static_assert(std::extent<decltype(w1::DecodeTable::fast)>::value == (1u << w1::kFastBits), "fast[] has 2^kFastBits entries");
static_assert(std::extent<decltype(w1::DecodeTable::first)>::value == cmp::kMaxCodeLen + 1, "first[] indexed by length 0..15");
static_assert(std::extent<decltype(w1::DecodeTable::count)>::value == cmp::kMaxCodeLen + 1, "count[] indexed by length 0..15");
static_assert(std::extent<decltype(w1::DecodeTable::offset)>::value == cmp::kMaxCodeLen + 1, "offset[] indexed by length 0..15");
static_assert(std::extent<decltype(w1::DecodeTable::sorted)>::value >= cmp::kNumLitLen, "sorted[] holds the biggest alphabet");

static_assert(cmp::kNumLitLen - 1 < (1u << 9), "a symbol must fit the 9 bits below the length in a fast entry");
static_assert(((cmp::kMaxCodeLen << 9) | 0x1FFu) <= 0xFFFFu, "(length << 9) | symbol must fit a uint16_t");
static_assert(sizeof(std::remove_extent<decltype(w1::Plan::litCode)>::type) * 8 >= cmp::kMaxCodeLen, "a 15-bit code fits litCode");
static_assert(sizeof(std::remove_extent<decltype(w1::Plan::distCode)>::type) * 8 >= cmp::kMaxCodeLen, "a 15-bit code fits distCode");
static_assert(std::extent<decltype(w1::Plan::litLen)>::value == cmp::kNumLitLen, "one length per lit/len symbol");
static_assert(std::extent<decltype(w1::Plan::distLen)>::value == cmp::kNumDist, "one length per distance symbol");
static_assert(std::is_same<decltype(w1::Plan::payloadBits), std::uint64_t>::value, "sizes are 64-bit");

// ---------- Plan: defaults and the size arithmetic ---------------------------------------
static void testPlanDefaults() {
    const w1::Plan p;
    CHECK(p.status == w1::Status::Ok);
    CHECK(p.headerBits == 0 && p.payloadBits == 0 && p.huffmanBits == 0 && p.symbolCount == 0);
    bool allZero = true;
    for (unsigned i = 0; i < cmp::kNumLitLen; ++i) allZero = allZero && p.litLen[i] == 0 && p.litCode[i] == 0;
    for (unsigned i = 0; i < cmp::kNumDist; ++i) allZero = allZero && p.distLen[i] == 0 && p.distCode[i] == 0;
    CHECK(allZero);
    CHECK(p.totalBits() == 0 && p.totalBytes() == 0);
}

// BVA on the rounding in totalBytes(): (bits + 7) / 8
static void testTotalBytesRounding() {
    struct Case { std::uint64_t bits; std::size_t bytes; };
    const Case cases[] = {{0, 0}, {1, 1}, {7, 1}, {8, 1}, {9, 2}, {15, 2}, {16, 2}, {17, 3}, {1263, 158}, {1264, 158}, {1265, 159}, {1281, 161}};
    for (const Case& c : cases) {
        w1::Plan p;
        p.payloadBits = c.bits;                      // header 0: pure rounding
        CHECK(p.totalBits() == c.bits);
        CHECK(p.totalBytes() == c.bytes);
    }
    w1::Plan big;
    big.headerBits = w1::kHeaderBits;
    big.payloadBits = (std::uint64_t{1} << 40) + 1;  // no 32-bit overflow anywhere
    CHECK(big.totalBits() == w1::kHeaderBits + (std::uint64_t{1} << 40) + 1);
    CHECK(big.totalBytes() == static_cast<std::size_t>((big.totalBits() + 7) / 8));
}

static void testTotalBitsIsHeaderPlusPayload() {
    w1::Plan p;
    p.headerBits = w1::kHeaderBits;
    p.payloadBits = 17;                              // the golden example
    CHECK(p.totalBits() == 1281);
    CHECK(p.totalBytes() == 161);
}

// plan() fills the struct exactly as the type promises (type + function working together)
static void testPlanFromGoldenBlock() {
    auto tb = newBlock();
    tb->addLiteral('a'); tb->addLiteral('b'); tb->addLiteral('c');
    tb->addMatch(9, 3);
    tb->addLiteral('x');
    const w1::Plan p = w1::plan(*tb);
    CHECK(p.status == w1::Status::Ok);
    CHECK(p.headerBits == w1::kHeaderBits);
    CHECK(p.totalBits() == p.headerBits + p.payloadBits);
    CHECK(p.huffmanBits <= p.payloadBits);           // payload also holds the extra bits
    CHECK(p.symbolCount == 7);
}

// ---------- EntropyReport ---------------------------------------------------------------
static void testEntropyReportDefaults() {
    const w1::EntropyReport e;
    CHECK(e.entropyBits == 0 && e.huffmanBits == 0 && e.symbols == 0);
    CHECK(e.entropyBitsPerSymbol == 0 && e.huffmanBitsPerSymbol == 0 && e.redundancyBitsPerSymbol == 0);
}

// ---------- DecodeTable / HuffTables -----------------------------------------------------
// Contract of the fast table: an entry is (length << 9) | symbol, and 0 means "no short code".
// A real entry can never be 0 because a length is at least 1.
static void testFastEntryPacking() {
    for (unsigned len = 1; len <= w1::kFastBits; ++len) {
        for (unsigned sym = 0; sym < cmp::kNumLitLen; ++sym) {
            const std::uint16_t e = static_cast<std::uint16_t>((len << 9) | sym);
            CHECK(e != 0);
            CHECK((e >> 9) == len);
            CHECK((e & 0x1FFu) == sym);
        }
    }
}

static void testEmptyDecodeTable() {
    std::unique_ptr<w1::DecodeTable> t(new w1::DecodeTable);
    std::uint8_t lens[cmp::kNumDist] = {};
    CHECK(w1::buildDecodeTable(lens, cmp::kNumDist, *t) == w1::Status::Ok);
    CHECK(t->maxLen == 0);                           // "0 = empty table"
    bool allZero = true;
    for (unsigned i = 0; i < (1u << w1::kFastBits); ++i) allZero = allZero && t->fast[i] == 0;
    CHECK(allZero);
}

// The slide says HuffTables is about 5 KB; measured here instead of assumed.
static void testHuffTablesSize() {
    CHECK(sizeof(w1::HuffTables) < 6144);
    CHECK(sizeof(w1::HuffTables) > 2 * sizeof(std::uint16_t) * (1u << w1::kFastBits));
    CHECK(sizeof(w1::HuffTables) == 2 * sizeof(w1::DecodeTable));
}

// ---------- statusName: every enumerator has its own readable name ------------------------
static void testStatusNames() {
    std::set<std::string> seen;
    for (int i = 0; i <= 8; ++i) {
        const std::string n = w1::statusName(static_cast<w1::Status>(i));
        CHECK(!n.empty() && n != "UNKNOWN");
        seen.insert(n);
    }
    CHECK(seen.size() == 9);
    CHECK(std::string(w1::statusName(w1::Status::Ok)) == "OK");
}

int main() {
    testPlanDefaults();
    testTotalBytesRounding();
    testTotalBitsIsHeaderPlusPayload();
    testPlanFromGoldenBlock();
    testEntropyReportDefaults();
    testFastEntryPacking();
    testEmptyDecodeTable();
    testHuffTablesSize();
    testStatusNames();
    return finishTests("test_types");
}
