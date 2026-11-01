// test_gaps.cpp - tests added after the coverage report (Step 2).
// Each test says which uncovered line/branch it closes and which course technique it uses.
// EP = equivalence partitioning, BVA = boundary value analysis, BP = basis path testing.
#include <set>
#include <string>
#include <vector>

#include "huffman.h"
#include "huffman_tree.h"
#include "min_heap.h"
#include "test_util.h"

// ---------- helpers ---------------------------------------------------------
// Builds a block by hand: a header with chosen code lengths, then a payload given as text
// such as "0110". This lets a test control every bit the decoder sees (no emit() needed).
// NOTE: every decodeBlock call in this file uses the same sink type (Recorder). That is
// deliberate: gcov drops branch information for template code (decodeBlock) that is
// instantiated with two different sink types in one program, and test_huffman.cpp already
// does that (Recorder + Strict). The "sink rejects a match" case is asserted there.
static w1::Status decodeHandBuilt(const std::uint8_t* lit, const std::uint8_t* dist,
                                  const std::string& payload) {
    std::vector<std::uint8_t> buf((w1::kHeaderBits + payload.size() + 7) / 8 + 8, 0);
    w1::BitWriter w(buf.data(), buf.size());
    for (unsigned i = 0; i < cmp::kNumLitLen; ++i) w.write(lit[i], 4);
    for (unsigned i = 0; i < cmp::kNumDist; ++i) w.write(dist[i], 4);
    for (char c : payload) w.write(c == '1' ? 1u : 0u, 1);
    const std::size_t bytes = w.finish();
    w1::BitReader r(buf.data(), bytes, w.bitCount());
    std::unique_ptr<w1::HuffTables> t(new w1::HuffTables);
    const w1::Status s = w1::readTables(r, *t);
    if (s != w1::Status::Ok) return s;
    Recorder rec;
    return w1::decodeBlock(r, *t, rec);
}

// The golden block (abcabcabcabcx) emitted into `buf`; returns the byte count.
static std::size_t buildGolden(std::vector<std::uint8_t>& buf, std::uint64_t& bits) {
    auto tb = newBlock();
    tb->addLiteral('a');
    tb->addLiteral('b');
    tb->addLiteral('c');
    tb->addMatch(9, 3);
    tb->addLiteral('x');
    const w1::Plan p = w1::plan(*tb);
    buf.assign(p.totalBytes(), 0);
    w1::BitWriter w(buf.data(), buf.size());
    CHECK(w1::emit(*tb, p, w) == w1::Status::Ok);
    const std::size_t bytes = w.finish();
    bits = w.bitCount();
    return bytes;
}

static w1::Status decodeGolden(const std::vector<std::uint8_t>& buf, std::size_t bytes,
                               std::uint64_t bits, bool onlyTables = false) {
    w1::BitReader r(buf.data(), bytes, bits);
    std::unique_ptr<w1::HuffTables> t(new w1::HuffTables);
    const w1::Status s = w1::readTables(r, *t);
    if (s != w1::Status::Ok || onlyTables) return s;
    Recorder rec;
    return w1::decodeBlock(r, *t, rec);
}

// ---------- 1. every Status has a readable, unique name (closes huffman.cpp 8-20) ----
static void testStatusNames() {
    std::set<std::string> seen;
    for (int i = 0; i <= 8; ++i) {
        const char* n = w1::statusName(static_cast<w1::Status>(i));
        CHECK(n != nullptr);
        CHECK(std::string(n) != "UNKNOWN");
        seen.insert(n);
    }
    CHECK(seen.size() == 9);                                   // all names different
    CHECK(std::string(w1::statusName(static_cast<w1::Status>(99))) == "UNKNOWN");
}

// ---------- 2. bit I/O gaps (closes bit_io.cpp:16 and bit_io.h peek(0)) --------------
static void testFinishOverflow() {
    std::uint8_t one[1] = {0};
    w1::BitWriter w(one, 1);
    w.write(0xABC, 12);               // 12 bits pending, nothing flushed yet
    CHECK(!w.overflow());
    w.finish();                       // needs 2 bytes, only 1 fits
    CHECK(w.overflow());
    CHECK(w.bitCount() == 12);
}

static void testPeekZero() {
    const std::uint8_t d[1] = {0xFF};
    w1::BitReader r(d, 1, 8);
    CHECK(r.peek(0) == 0);            // BVA: n = 0
    r.refill();
    CHECK(r.peek(8) == 0xFF);
}

// ---------- 3. decoder statuses asserted exactly (BP on decodeSymbol/decodeBlock) ----
static void testDecoderStatuses() {
    std::uint8_t lit[cmp::kNumLitLen] = {};
    std::uint8_t dist[cmp::kNumDist] = {};

    // Only EOB has a code: length 1, code "0".
    lit[256] = 1;
    CHECK(decodeHandBuilt(lit, dist, "0") == w1::Status::Ok);
    CHECK(decodeHandBuilt(lit, dist, "01") == w1::Status::TrailingBits);   // junk after EOB
    CHECK(decodeHandBuilt(lit, dist, "1") == w1::Status::InvalidCode);     // path: fast miss, loop skipped

    // Only EOB, but with a 12-bit code: exercises the slow (canonical) path.
    lit[256] = 12;
    CHECK(decodeHandBuilt(lit, dist, "000000000000") == w1::Status::Ok);               // slow hit
    CHECK(decodeHandBuilt(lit, dist, "00000000000") == w1::Status::InvalidBitLength);  // slow hit, too few bits
    CHECK(decodeHandBuilt(lit, dist, "111111111111") == w1::Status::InvalidCode);      // slow loop, no match

    // EOB (code 0) and length symbol 257 (code 1), but NO distance codes.
    lit[256] = 1; lit[257] = 1;
    CHECK(decodeHandBuilt(lit, dist, "1") == w1::Status::InvalidCodeTable);

    // Length symbol 265 needs 1 extra bit, but the payload ends right after its code.
    lit[257] = 0; lit[265] = 1;
    CHECK(decodeHandBuilt(lit, dist, "1") == w1::Status::InvalidBitLength);

    // Distance symbol 4 needs 1 extra bit, but the payload ends after its code.
    lit[265] = 0; lit[257] = 1; dist[4] = 1;
    CHECK(decodeHandBuilt(lit, dist, "10") == w1::Status::InvalidBitLength);

    // The DISTANCE symbol itself cannot be decoded: only distance symbol 0 has a code ("0"),
    // so a payload bit 1 after the length symbol matches nothing.
    dist[4] = 0; dist[0] = 1;
    CHECK(decodeHandBuilt(lit, dist, "11") == w1::Status::InvalidCode);
}

// ---------- 4. boundary values on payloadBits (BVA) ----------------------------------
static void testPayloadBitsBoundaries() {
    std::vector<std::uint8_t> buf;
    std::uint64_t bits = 0;
    const std::size_t bytes = buildGolden(buf, bits);
    CHECK(bits == 1281);

    CHECK(decodeGolden(buf, bytes, bits) == w1::Status::Ok);                       // exact
    CHECK(decodeGolden(buf, bytes, bits - 1) == w1::Status::InvalidBitLength);     // last bit cut: EOB incomplete
    CHECK(decodeGolden(buf, bytes, 1263, true) == w1::Status::InvalidBitLength);   // header 1 bit short
    CHECK(decodeGolden(buf, bytes, 1264, true) == w1::Status::Ok);                 // header exactly
    CHECK(decodeGolden(buf, bytes, 1264) == w1::Status::InvalidBitLength);         // header but no payload
    CHECK(decodeGolden(buf, bytes, bytes * 8 + 1, true) == w1::Status::InvalidBitLength);  // more bits than data
}

// ---------- 5. emit() error branches (huffman.cpp 60, 81, 86) ------------------------
static void testEmitErrors() {
    auto tb = newBlock();
    tb->addLiteral('a');
    std::vector<std::uint8_t> buf(w1::kHeaderBits / 8 + 64);

    // guard, first operand: a plan that is not Ok
    w1::Plan p = w1::plan(*tb);
    p.status = w1::Status::PlanMismatch;
    { w1::BitWriter w(buf.data(), buf.size()); CHECK(w1::emit(*tb, p, w) == w1::Status::InvalidToken); }

    // guard, second operand: more tokens than a block can hold
    p = w1::plan(*tb);
    tb->n = cmp::kBlockTokens + 1;
    { w1::BitWriter w(buf.data(), buf.size()); CHECK(w1::emit(*tb, p, w) == w1::Status::InvalidToken); }

    // a match whose LENGTH symbol was never counted
    auto m1 = newBlock();
    m1->addLiteral('a');
    m1->tok[m1->n++] = cmp::mkMatch(9, 3);
    const w1::Plan p1 = w1::plan(*m1);
    { w1::BitWriter w(buf.data(), buf.size()); CHECK(w1::emit(*m1, p1, w) == w1::Status::MissingCode); }

    // a match whose length symbol was counted but whose DISTANCE symbol was not
    auto m2 = newBlock();
    m2->addLiteral('a');
    m2->tok[m2->n++] = cmp::mkMatch(9, 3);
    ++m2->litlenFreq[cmp::lenSymbol(9)];
    const w1::Plan p2 = w1::plan(*m2);
    { w1::BitWriter w(buf.data(), buf.size()); CHECK(w1::emit(*m2, p2, w) == w1::Status::MissingCode); }

    // BVA on the distance field: field 32767 = distance 32768 is legal, field 32768 is not
    auto ok = newBlock();
    ok->addMatch(3, 32768);
    std::vector<cmp::Tok> out;
    CHECK(roundTrip(*ok, out));
    auto bad = newBlock();
    bad->tok[bad->n++] = cmp::kMatchFlag | 32768u;            // distance 32769
    const w1::Plan pb = w1::plan(*bad);
    { w1::BitWriter w(buf.data(), buf.size()); CHECK(w1::emit(*bad, pb, w) == w1::Status::InvalidToken); }
}

// ---------- 6. plan/entropy defensive branches (huffman.cpp 31, 180, 196) ------------
static void testEobAndEmptyPlan() {
    auto tb = newBlock();
    tb->addLiteral('a');
    tb->addLiteral('b');
    tb->litlenFreq[cmp::kEob] = 0;                            // a block built without reset()'s EOB count
    w1::Plan p;
    std::vector<cmp::Tok> out;
    CHECK(roundTrip(*tb, out, &p));                           // plan() repairs it, emit/decode still agree
    CHECK(p.litLen[cmp::kEob] > 0);
    CHECK(out.size() == 2);

    const w1::EntropyReport e = w1::entropyReport(*tb, p);
    CHECK(e.symbols == p.symbolCount && e.symbols > 0);

    const w1::Plan none;                                      // symbolCount == 0
    const w1::EntropyReport z = w1::entropyReport(*tb, none);
    CHECK(z.symbols == 0 && z.entropyBitsPerSymbol == 0 && z.redundancyBitsPerSymbol == 0);
}

// ---------- 7. verifyPrefixFree(Plan) false sides (huffman.cpp 171-172) --------------
static void testVerifyPlanNegative() {
    auto tb = newBlock();
    tb->addLiteral('a'); tb->addLiteral('b'); tb->addLiteral('c');
    tb->addMatch(9, 3); tb->addLiteral('x');
    const w1::Plan p = w1::plan(*tb);
    CHECK(w1::verifyPrefixFree(p));

    w1::Plan badLit = p;
    badLit.litCode['a'] = badLit.litCode['b'];                // two symbols share a code
    CHECK(!w1::verifyPrefixFree(badLit));

    w1::Plan badDist = p;
    badDist.distLen[0] = 1;                                   // three 1-bit distance codes
    badDist.distLen[1] = 1;
    CHECK(!w1::verifyPrefixFree(badDist));
}

// ---------- 8. buildDecodeTable length check (huffman.cpp 106) -----------------------
static void testDecodeTableBadLength() {
    std::uint8_t lens[cmp::kNumLitLen] = {};
    lens[0] = 16;                                             // BVA: 15 is the maximum
    std::unique_ptr<w1::DecodeTable> t(new w1::DecodeTable);
    CHECK(w1::buildDecodeTable(lens, cmp::kNumLitLen, *t) == w1::Status::InvalidCodeTable);
    lens[0] = 15;
    CHECK(w1::buildDecodeTable(lens, cmp::kNumLitLen, *t) == w1::Status::Ok);
}

// ---------- 9. min-heap capacity (min_heap.cpp 6) ------------------------------------
static void testHeapCapacity() {
    w1::MinHeap h;
    for (unsigned i = 0; i < w1::MinHeap::kCapacity; ++i) {
        CHECK(h.push(w1::HeapItem{i, i}));
    }
    CHECK(!h.push(w1::HeapItem{0, 999}));                     // 321st item is refused
    CHECK(h.size() == w1::MinHeap::kCapacity);
}

// ---------- 10. buildLengths: pinned answers and guards (huffman_tree.cpp 16,21,77) --
static std::uint64_t cost(const std::uint32_t* f, const std::uint8_t* l, unsigned n) {
    std::uint64_t c = 0;
    for (unsigned i = 0; i < n; ++i) c += static_cast<std::uint64_t>(f[i]) * l[i];
    return c;
}

static void testBuildLengthsPinned() {
    std::uint8_t l[5];
    const std::uint32_t f[5] = {1, 1, 2, 4, 8};

    // no limit needed (maxLen above 15 is clamped to 15): natural Huffman lengths
    w1::buildLengths(f, 5, 20, l);
    const std::uint8_t natural[5] = {4, 4, 3, 2, 1};
    for (int i = 0; i < 5; ++i) CHECK(l[i] == natural[i]);
    CHECK(cost(f, l, 5) == 30);

    // limit 3: A and B are too deep. One fix-up step; rarest four symbols get length 3.
    w1::buildLengths(f, 5, 3, l);
    const std::uint8_t limited3[5] = {3, 3, 3, 3, 1};
    for (int i = 0; i < 5; ++i) CHECK(l[i] == limited3[i]);
    CHECK(cost(f, l, 5) == 32);                               // 2 bits worse than optimal
    CHECK(w1::kraftSum(l, 5) == (1u << 15));

    // limit 2 on four symbols: the fix-up scan starts at level 1 (bits > 1 is false)
    const std::uint32_t g[4] = {1, 1, 2, 4};
    std::uint8_t l4[4];
    w1::buildLengths(g, 4, 2, l4);
    for (int i = 0; i < 4; ++i) CHECK(l4[i] == 2);
    CHECK(w1::kraftSum(l4, 4) == (1u << 15));

    // an alphabet bigger than 286 is cut off: symbols from 286 up are ignored
    std::vector<std::uint32_t> big(300, 0);
    big[0] = 1; big[1] = 1; big[299] = 5;
    std::vector<std::uint8_t> bl(300, 9);
    w1::buildLengths(big.data(), 300, 15, bl.data());
    CHECK(bl[0] == 1 && bl[1] == 1 && bl[299] == 0);
}

// ---------- 11. reference trie edge cases (huffman_tree.cpp 143,155,165,167) ---------
static void testTrieEdges() {
    w1::CodeTrie t;
    {   // a code that is a prefix of a later code: "0" then "01"
        const std::uint8_t lens[2] = {1, 2};
        const std::uint16_t codes[2] = {0b0, 0b01};
        CHECK(!w1::buildTrie(lens, codes, 2, t));
    }
    {   // the same code twice
        const std::uint8_t lens[2] = {2, 2};
        const std::uint16_t codes[2] = {0b10, 0b10};
        CHECK(!w1::buildTrie(lens, codes, 2, t));
    }
    {   // a later code that is a prefix of an earlier one: "10" then "1"
        const std::uint8_t lens[2] = {2, 1};
        const std::uint16_t codes[2] = {0b10, 0b1};
        CHECK(!w1::buildTrie(lens, codes, 2, t));
    }
    {   // same, through the other child: "11" then "1"
        const std::uint8_t lens[2] = {2, 1};
        const std::uint16_t codes[2] = {0b11, 0b1};
        CHECK(!w1::buildTrie(lens, codes, 2, t));
    }
    {   // decoding with no bits left returns -1
        const std::uint8_t lens[3] = {1, 2, 2};
        std::uint16_t codes[3];
        w1::assignCanonical(lens, 3, codes);
        CHECK(w1::buildTrie(lens, codes, 3, t));
        const std::uint8_t d[1] = {0};
        w1::BitReader r(d, 1, 0);
        CHECK(w1::trieDecode(t, r) == -1);
    }
    {   // incomplete code {00, 01}: a leading 1 falls off the trie
        const std::uint8_t lens[2] = {2, 2};
        std::uint16_t codes[2];
        w1::assignCanonical(lens, 2, codes);
        CHECK(w1::buildTrie(lens, codes, 2, t));
        const std::uint8_t d[1] = {0x80};
        w1::BitReader r(d, 1, 1);
        CHECK(w1::trieDecode(t, r) == -1);
    }
}

// ---------- 12. blocks exactly at capacity (BVA on token count) ----------------------
static void testFullBlocks() {
    std::vector<cmp::Tok> out;

    auto lits = newBlock();                                   // 32768 literal words
    for (unsigned i = 0; i < cmp::kBlockTokens; ++i) lits->addLiteral(static_cast<std::uint8_t>(i * 7));
    CHECK(lits->full() && lits->n == cmp::kBlockTokens);
    CHECK(roundTrip(*lits, out));
    CHECK(out.size() == cmp::kBlockTokens);

    auto pairs = newBlock();                                  // match+literal pairs until no room
    while (pairs->hasRoomForToken()) pairs->addToken(cmp::Token{3, 9, 'x'});
    CHECK(pairs->n == cmp::kBlockTokens);
    CHECK(roundTrip(*pairs, out));
    bool same = out.size() == pairs->n;
    for (std::uint32_t i = 0; same && i < pairs->n; ++i) same = out[i] == pairs->tok[i];
    CHECK(same);
}

int main() {
    testStatusNames();
    testFinishOverflow();
    testPeekZero();
    testDecoderStatuses();
    testPayloadBitsBoundaries();
    testEmitErrors();
    testEobAndEmptyPlan();
    testVerifyPlanNegative();
    testDecodeTableBadLength();
    testHeapCapacity();
    testBuildLengthsPinned();
    testTrieEdges();
    testFullBlocks();
    return finishTests("test_gaps");
}
