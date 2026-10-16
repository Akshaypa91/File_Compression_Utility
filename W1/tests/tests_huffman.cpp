// End-to-end tests for W1: the golden vector, random round trips, edge cases, error
// handling, corruption robustness and the entropy report.
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "huffman.h"
#include "test_util.h"

// ---------------------------------------------------------------------------
// Golden vector: input "abcabcabcabcx"
//   tokens: lit a, lit b, lit c, match(len 9, dist 3), lit x, EOB
// This is the shared oracle from the design README; every team member must be able to
// explain it.
// ---------------------------------------------------------------------------
static void testGoldenVector() {
    auto tb = newBlock();
    tb->addLiteral('a');
    tb->addLiteral('b');
    tb->addLiteral('c');
    tb->addMatch(9, 3);
    tb->addLiteral('x');

    CHECK(cmp::lenSymbol(9) == 263);
    CHECK(cmp::distSymbol(3) == 2);
    CHECK(tb->extraBitsTotal == 0);

    const w1::Plan p = w1::plan(*tb);
    CHECK(p.status == w1::Status::Ok);
    CHECK(p.litLen[256] == 2 && p.litLen[263] == 2);
    CHECK(p.litLen['a'] == 3 && p.litLen['b'] == 3 && p.litLen['c'] == 3 && p.litLen['x'] == 3);
    CHECK(p.distLen[2] == 1 && p.distCode[2] == 0);
    CHECK(p.litCode[256] == 0b00 && p.litCode[263] == 0b01);
    CHECK(p.litCode['a'] == 0b100 && p.litCode['b'] == 0b101);
    CHECK(p.litCode['c'] == 0b110 && p.litCode['x'] == 0b111);
    CHECK(p.headerBits == 1264);                  // 316 lengths * 4 bits = 158 bytes
    CHECK(p.payloadBits == 17);
    CHECK(p.symbolCount == 7);                    // 6 litlen symbols + 1 distance symbol
    CHECK(w1::verifyPrefixFree(p));

    std::vector<std::uint8_t> buf(p.totalBytes());
    w1::BitWriter w(buf.data(), buf.size());
    CHECK(w1::emit(*tb, p, w) == w1::Status::Ok);
    CHECK(w.finish() == 161);
    CHECK(w.bitCount() == 1281);                  // 1264 header bits + 17 payload bits

    // payload bits 100 101 110 01 0 111 00 = 10010111001011100, padded: 97 2E 00
    CHECK(buf[158] == 0x97);
    CHECK(buf[159] == 0x2E);
    CHECK(buf[160] == 0x00);

    // decode it back
    w1::BitReader r(buf.data(), buf.size(), w.bitCount());
    std::unique_ptr<w1::HuffTables> t(new w1::HuffTables);
    CHECK(w1::readTables(r, *t) == w1::Status::Ok);
    Recorder rec;
    CHECK(w1::decodeBlock(r, *t, rec) == w1::Status::Ok);
    CHECK(rec.toks.size() == 5);
    CHECK(rec.toks[0] == cmp::mkLit('a') && rec.toks[1] == cmp::mkLit('b') && rec.toks[2] == cmp::mkLit('c'));
    CHECK(rec.toks[3] == cmp::mkMatch(9, 3));
    CHECK(rec.toks[4] == cmp::mkLit('x'));

    // the padding bits are never read: payloadBits stops decoding exactly at EOB
    CHECK(r.bitsLeft() == 0);
}

static void testEmptyAndTinyBlocks() {
    // a block with no tokens: only EOB, a single symbol with a 1-bit code
    auto tb = newBlock();
    std::vector<cmp::Tok> out;
    CHECK(roundTrip(*tb, out));
    CHECK(out.empty());

    // one literal
    tb->addLiteral(0x00);
    CHECK(roundTrip(*tb, out));
    CHECK(out.size() == 1 && out[0] == cmp::mkLit(0x00));

    // one repeated literal (single used literal symbol + EOB)
    auto rep = newBlock();
    for (int i = 0; i < 1000; ++i) rep->addLiteral('q');
    CHECK(roundTrip(*rep, out));
    CHECK(out.size() == 1000);

    // matches only, one length and one distance symbol used (single-symbol distance code)
    auto m = newBlock();
    for (int i = 0; i < 50; ++i) m->addMatch(258, 1);
    CHECK(roundTrip(*m, out));
    CHECK(out.size() == 50 && out[0] == cmp::mkMatch(258, 1));
}

static void testAllLengthsAndDistances() {
    // every legal length and a spread of distances including all bucket boundaries
    auto tb = newBlock();
    for (std::uint32_t len = 3; len <= 258; ++len) tb->addMatch(len, 1 + (len * 131) % 32768);
    std::vector<cmp::Tok> out;
    CHECK(roundTrip(*tb, out));
    CHECK(out.size() == tb->n);
    for (std::uint32_t i = 0; i < tb->n; ++i) CHECK(out[i] == tb->tok[i]);

    auto td = newBlock();
    for (unsigned c = 0; c < cmp::kNumDist; ++c) {
        const std::uint32_t lo = cmp::kTables.distBase[c];
        const std::uint32_t hi = lo + (1u << cmp::kTables.distExtra[c]) - 1;
        td->addMatch(3, lo);
        td->addMatch(258, hi);
        td->addMatch(10, lo + (hi - lo) / 2);
        CHECK(cmp::distSymbol(lo) == c && cmp::distSymbol(hi) == c);
    }
    CHECK(roundTrip(*td, out));
    for (std::uint32_t i = 0; i < td->n; ++i) CHECK(out[i] == td->tok[i]);
    CHECK(cmp::distSymbol(32768) == 29);

    for (std::uint32_t len = 3; len <= 258; ++len) {
        const unsigned s = cmp::lenSymbol(len);
        CHECK(s >= 257 && s <= 285);
        const unsigned li = s - 257;
        CHECK(len >= cmp::kTables.lenBase[li]);
        CHECK(len - cmp::kTables.lenBase[li] < (1u << cmp::kTables.lenExtra[li]) || len == 258);
    }
}

static void testRandomRoundTrips() {
    std::mt19937 rng(2026);
    auto tb = newBlock();
    std::vector<cmp::Tok> out;
    for (int round = 0; round < 300; ++round) {
        const int mode = round % 6;
        const unsigned count = 1 + rng() % (mode == 5 ? 1 : 30000);
        fillRandomBlock(*tb, rng, mode, count);
        w1::Plan p;
        CHECK(roundTrip(*tb, out, &p));
        CHECK(out.size() == tb->n);
        bool same = out.size() == tb->n;
        for (std::uint32_t i = 0; same && i < tb->n; ++i) same = out[i] == tb->tok[i];
        CHECK(same);
        CHECK(w1::verifyPrefixFree(p));
        for (unsigned i = 0; i < cmp::kNumLitLen; ++i) CHECK(p.litLen[i] <= 15);
        for (unsigned i = 0; i < cmp::kNumDist; ++i) CHECK(p.distLen[i] <= 15);
    }
}

static void testLengthLimitedBlock() {
    // Fibonacci-skewed literals force the 15-bit limit; the block must still round-trip
    auto tb = newBlock();
    std::mt19937 rng(1);
    fillRandomBlock(*tb, rng, 5, 0);
    w1::Plan p = w1::plan(*tb);
    unsigned maxLen = 0;
    for (unsigned i = 0; i < cmp::kNumLitLen; ++i) if (p.litLen[i] > maxLen) maxLen = p.litLen[i];
    CHECK(maxLen == 15);                  // the limit was actually reached, so the fix-up ran
    CHECK(w1::verifyPrefixFree(p));
    std::vector<cmp::Tok> out;
    CHECK(roundTrip(*tb, out));
    CHECK(out.size() == tb->n);
}

static void testEntropyReport() {
    auto tb = newBlock();
    std::mt19937 rng(3);
    fillRandomBlock(*tb, rng, 3, 20000);               // skewed literals
    const w1::Plan p = w1::plan(*tb);
    const w1::EntropyReport e = w1::entropyReport(*tb, p);
    CHECK(e.symbols == p.symbolCount);
    // Huffman never beats the entropy bound, and is within 1 bit/symbol of it
    CHECK(static_cast<double>(e.huffmanBits) + 1e-6 >= e.entropyBits);
    CHECK(e.redundancyBitsPerSymbol >= -1e-9);
    CHECK(e.redundancyBitsPerSymbol < 1.0);
    CHECK(e.entropyBitsPerSymbol > 0 && e.huffmanBitsPerSymbol > 0);

    // four equally likely literals: the redundancy stays below 1 bit per symbol
    auto u = newBlock();
    for (int i = 0; i < 100; ++i) { u->addLiteral('a'); u->addLiteral('b'); u->addLiteral('c'); u->addLiteral('d'); }
    const w1::Plan pu = w1::plan(*u);
    const w1::EntropyReport eu = w1::entropyReport(*u, pu);
    CHECK(eu.redundancyBitsPerSymbol >= -1e-9 && eu.redundancyBitsPerSymbol < 1.0);
}

static void testErrorHandling() {
    auto tb = newBlock();
    tb->addLiteral('a');
    tb->addLiteral('b');
    const w1::Plan p = w1::plan(*tb);

    // buffer too small
    std::vector<std::uint8_t> tiny(10);
    w1::BitWriter wt(tiny.data(), tiny.size());
    CHECK(w1::emit(*tb, p, wt) == w1::Status::BufferOverflow);

    // a token whose symbol has no code (the frequency table lies)
    auto bad = newBlock();
    bad->addLiteral('a');
    bad->tok[bad->n++] = cmp::mkLit('z');              // token added without counting it
    const w1::Plan pb = w1::plan(*bad);
    std::vector<std::uint8_t> buf(pb.totalBytes() + 16);
    w1::BitWriter wb(buf.data(), buf.size());
    CHECK(w1::emit(*bad, pb, wb) == w1::Status::MissingCode);

    // frequencies that overcount: plan size differs from what was written
    auto over = newBlock();
    over->addLiteral('a');
    over->addLiteral('b');
    ++over->litlenFreq['a'];                           // claims 'a' twice
    const w1::Plan po = w1::plan(*over);
    std::vector<std::uint8_t> buf2(po.totalBytes() + 16);
    w1::BitWriter wo(buf2.data(), buf2.size());
    CHECK(w1::emit(*over, po, wo) == w1::Status::PlanMismatch);

    // a distance above 32768 can never be emitted (this would index out of the table)
    auto d = newBlock();
    d->tok[d->n++] = cmp::kMatchFlag | (0u << 16) | 40000u;
    const w1::Plan pd = w1::plan(*d);
    std::vector<std::uint8_t> buf3(pd.totalBytes() + 16);
    w1::BitWriter wd(buf3.data(), buf3.size());
    CHECK(w1::emit(*d, pd, wd) == w1::Status::InvalidToken);
}

// A valid block, then damage it in every way a bad file could: the decoder must return an
// error or a result, never crash or read out of bounds (run this under ASan/UBSan).
static void testCorruptionRobustness() {
    std::mt19937 rng(77);
    auto tb = newBlock();
    fillRandomBlock(*tb, rng, 1, 3000);
    const w1::Plan p = w1::plan(*tb);
    std::vector<std::uint8_t> good(p.totalBytes());
    w1::BitWriter w(good.data(), good.size());
    CHECK(w1::emit(*tb, p, w) == w1::Status::Ok);
    w.finish();
    const std::uint64_t bits = w.bitCount();

    std::unique_ptr<w1::HuffTables> t(new w1::HuffTables);
    long ok = 0, rejected = 0;
    for (int it = 0; it < 4000; ++it) {
        std::vector<std::uint8_t> d = good;
        std::uint64_t useBits = bits;
        const int kind = it % 4;
        if (kind == 0) {                                   // flip random bits anywhere
            for (int k = 0; k < 1 + static_cast<int>(rng() % 4); ++k) d[rng() % d.size()] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
        } else if (kind == 1) {                            // truncate the declared bit length
            useBits = rng() % (bits + 1);
        } else if (kind == 2) {                            // flip bits only inside the header
            for (int k = 0; k < 3; ++k) d[rng() % 158] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
        } else {                                           // pure garbage
            for (auto& b : d) b = static_cast<std::uint8_t>(rng());
        }
        w1::BitReader r(d.data(), d.size(), useBits);
        const w1::Status s = w1::readTables(r, *t);
        if (s != w1::Status::Ok) { ++rejected; continue; }
        Recorder rec;
        const w1::Status s2 = w1::decodeBlock(r, *t, rec);
        (s2 == w1::Status::Ok ? ok : rejected)++;
        CHECK(rec.toks.size() <= useBits);                 // every token costs at least one bit
    }
    CHECK(rejected > 0);                                   // corruption is detected, not ignored
    (void)ok;

    // the sink can reject an impossible reference: decoding stops with INVALID_TOKEN
    struct Strict {
        std::uint64_t produced = 0;
        void lit(std::uint8_t) { ++produced; }
        bool match(std::uint32_t len, std::uint32_t dist) {
            if (dist > produced) return false;
            produced += len;
            return true;
        }
    };
    auto m = newBlock();
    m->addMatch(5, 100);                                   // distance 100 with nothing produced yet
    const w1::Plan pm = w1::plan(*m);
    std::vector<std::uint8_t> bm(pm.totalBytes());
    w1::BitWriter wm(bm.data(), bm.size());
    CHECK(w1::emit(*m, pm, wm) == w1::Status::Ok);
    wm.finish();
    w1::BitReader rm(bm.data(), bm.size(), wm.bitCount());
    CHECK(w1::readTables(rm, *t) == w1::Status::Ok);
    Strict strict;
    CHECK(w1::decodeBlock(rm, *t, strict) == w1::Status::InvalidToken);
}

static void testMalformedTables() {
    std::vector<std::uint8_t> raw(158 + 8, 0);
    auto setLen = [&](unsigned nibbleIndex, unsigned v) {
        std::uint8_t& b = raw[nibbleIndex / 2];
        b = (nibbleIndex % 2 == 0) ? static_cast<std::uint8_t>((b & 0x0F) | (v << 4))
                                   : static_cast<std::uint8_t>((b & 0xF0) | v);
    };
    std::unique_ptr<w1::HuffTables> t(new w1::HuffTables);

    // no EOB code: the block could never end
    {
        w1::BitReader r(raw.data(), raw.size(), 1264 + 8);
        CHECK(w1::readTables(r, *t) == w1::Status::InvalidCodeTable);
    }
    // over-subscribed literal code: three symbols of length 1
    setLen(256, 1);
    setLen(0, 1);
    setLen(1, 1);
    {
        w1::BitReader r(raw.data(), raw.size(), 1264 + 8);
        CHECK(w1::readTables(r, *t) == w1::Status::InvalidCodeTable);
    }
    // header shorter than the declared bits
    {
        w1::BitReader r(raw.data(), raw.size(), 100);
        CHECK(w1::readTables(r, *t) == w1::Status::InvalidBitLength);
    }
    // payloadBits larger than the payload itself
    {
        w1::BitReader r(raw.data(), raw.size(), raw.size() * 8 + 1);
        CHECK(w1::readTables(r, *t) == w1::Status::InvalidBitLength);
    }
}

int main() {
    testGoldenVector();
    testEmptyAndTinyBlocks();
    testAllLengthsAndDistances();
    testRandomRoundTrips();
    testLengthLimitedBlock();
    testEntropyReport();
    testErrorHandling();
    testCorruptionRobustness();
    testMalformedTables();
    return finishTests("test_huffman");
}