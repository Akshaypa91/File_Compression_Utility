// evidence.cpp - prints "case | input | expected | ACTUAL | PASS/FAIL" tables for W1's boundary
// and combination tests, so every output value in the test report was observed by running
// this program on the team's own machine.
//
// Build (from the repo root):
//   g++ -std=c++17 -O0 -I W1/include -I shared/include W1/src/*.cpp W1/examples/evidence.cpp -o build_evidence
// Run:   ./build_evidence        (exit code 0 = every row passed)
//
// EXPECTED values come from the specification: the Status comments in huffman_types.h and the
// limits in symbols.hpp. ACTUAL values are whatever W1 returns when this program runs.
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "huffman.h"

namespace {

struct Row { std::string id, input, expected, actual; bool pass; };
std::vector<Row> rows;
int failures = 0;

void add(const std::string& id, const std::string& input, const std::string& expected,
         const std::string& actual, bool pass) {
    rows.push_back({id, input, expected, actual, pass});
    if (!pass) ++failures;
}
void addEq(const std::string& id, const std::string& input, const std::string& expected,
           const std::string& actual) {
    add(id, input, expected, actual, expected == actual);
}
void printAndClear(const char* title) {
    std::printf("\n== %s\n", title);
    std::printf("%-5s %-46s %-20s %-34s %s\n", "case", "input", "expected", "ACTUAL", "result");
    for (const Row& r : rows) {
        std::printf("%-5s %-46s %-20s %-34s %s\n", r.id.c_str(), r.input.c_str(),
                    r.expected.c_str(), r.actual.c_str(), r.pass ? "PASS" : "FAIL");
    }
    rows.clear();
}

struct Sink {
    std::vector<cmp::Tok> toks;
    void lit(std::uint8_t b) { toks.push_back(cmp::mkLit(b)); }
    bool match(std::uint32_t l, std::uint32_t d) { toks.push_back(cmp::mkMatch(l, d)); return true; }
};

std::unique_ptr<cmp::TokenBlock> newBlock() {
    std::unique_ptr<cmp::TokenBlock> b(new cmp::TokenBlock);
    b->reset();
    return b;
}

const char* emitStatus(const cmp::TokenBlock& tb) {
    const w1::Plan p = w1::plan(tb);
    std::vector<std::uint8_t> buf(p.totalBytes() + 16);
    w1::BitWriter w(buf.data(), buf.size());
    return w1::statusName(w1::emit(tb, p, w));
}

// golden block abcabcabcabcx emitted into an exactly-sized buffer
std::size_t golden(std::vector<std::uint8_t>& buf, std::uint64_t& bits) {
    auto tb = newBlock();
    tb->addLiteral('a'); tb->addLiteral('b'); tb->addLiteral('c');
    tb->addMatch(9, 3);
    tb->addLiteral('x');
    const w1::Plan p = w1::plan(*tb);
    buf.assign(p.totalBytes(), 0);
    w1::BitWriter w(buf.data(), buf.size());
    w1::emit(*tb, p, w);
    const std::size_t n = w.finish();
    bits = w.bitCount();
    return n;
}

const char* decodeStatus(const std::vector<std::uint8_t>& buf, std::size_t bytes,
                         std::uint64_t bits, bool tablesOnly) {
    w1::BitReader r(buf.data(), bytes, bits);
    std::unique_ptr<w1::HuffTables> t(new w1::HuffTables);
    const w1::Status s = w1::readTables(r, *t);
    if (s != w1::Status::Ok || tablesOnly) return w1::statusName(s);
    Sink sink;
    return w1::statusName(w1::decodeBlock(r, *t, sink));
}

// ---- orthogonal array: same builder as tests/test_oat.cpp ------------------------------
std::unique_ptr<cmp::TokenBlock> oatBlock(int a, int b, int c, int d, unsigned seed) {
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
    auto len = [&]() -> std::uint32_t { return b == 1 ? 3 : b == 2 ? 3 + rng() % 256 : 258; };
    auto dist = [&]() -> std::uint32_t { return c == 1 ? 1 : c == 2 ? 1 + rng() % 32768 : 32768; };
    const unsigned target = d == 1 ? 4 : d == 2 ? 5000 : cmp::kBlockTokens;
    unsigned i = 0;
    while (tb->n < target && tb->hasRoomForToken()) {
        if (i++ % 2 == 0) {
            const std::uint32_t dd = dist();
            const std::uint32_t ll = len();
            tb->addToken(cmp::Token{dd, ll, lit()});
        } else {
            tb->addToken(cmp::Token{0, 0, lit()});
        }
    }
    while (d == 3 && !tb->full()) tb->addLiteral(lit());
    return tb;
}

bool lossless(const cmp::TokenBlock& tb, const w1::Plan& p) {
    std::vector<std::uint8_t> buf(p.totalBytes());
    w1::BitWriter w(buf.data(), buf.size());
    if (w1::emit(tb, p, w) != w1::Status::Ok) return false;
    const std::size_t bytes = w.finish();
    w1::BitReader r(buf.data(), bytes, w.bitCount());
    std::unique_ptr<w1::HuffTables> t(new w1::HuffTables);
    if (w1::readTables(r, *t) != w1::Status::Ok) return false;
    Sink sink;
    if (w1::decodeBlock(r, *t, sink) != w1::Status::Ok) return false;
    if (sink.toks.size() != tb.n) return false;
    for (std::uint32_t i = 0; i < tb.n; ++i) if (sink.toks[i] != tb.tok[i]) return false;
    return true;
}

}  // namespace

int main() {
    // ---- E3 / E4 / E6: limits ------------------------------------------------------------
    {
        auto ok = newBlock();  ok->addMatch(3, 32768);
        addEq("E3a", "distance field 32767 (distance 32768)", "OK", emitStatus(*ok));
        auto b1 = newBlock();  b1->tok[b1->n++] = cmp::kMatchFlag | 32768u;
        addEq("E3b", "distance field 32768 (distance 32769)", "INVALID_TOKEN", emitStatus(*b1));
        auto b2 = newBlock();  b2->tok[b2->n++] = cmp::kMatchFlag | 40000u;
        addEq("E3c", "distance field 40000", "INVALID_TOKEN", emitStatus(*b2));

        auto full = newBlock();
        for (unsigned i = 0; i < cmp::kBlockTokens; ++i) full->addLiteral(static_cast<std::uint8_t>(i));
        addEq("E4a", "block with exactly 32768 words", "OK", emitStatus(*full));
        auto over = newBlock();  over->addLiteral('a');  over->n = cmp::kBlockTokens + 1;
        addEq("E4b", "block claiming 32769 words", "INVALID_TOKEN", emitStatus(*over));

        std::unique_ptr<w1::DecodeTable> t(new w1::DecodeTable);
        std::uint8_t lens[cmp::kNumLitLen] = {};
        lens[0] = 15;
        addEq("E6a", "code length 15", "OK", w1::statusName(w1::buildDecodeTable(lens, cmp::kNumLitLen, *t)));
        lens[0] = 16;
        addEq("E6b", "code length 16", "INVALID_CODE_TABLE", w1::statusName(w1::buildDecodeTable(lens, cmp::kNumLitLen, *t)));
    }
    printAndClear("Limits: distance field (E3), words per block (E4), code length (E6)");

    // ---- E7: payloadBits ---------------------------------------------------------------
    {
        std::vector<std::uint8_t> buf;  std::uint64_t bits = 0;
        const std::size_t bytes = golden(buf, bits);
        addEq("E7a", "golden, payloadBits = " + std::to_string(bits) + " (exact)", "OK", decodeStatus(buf, bytes, bits, false));
        addEq("E7b", "golden, payloadBits = bits - 1", "INVALID_BIT_LENGTH", decodeStatus(buf, bytes, bits - 1, false));
        addEq("E7c", "header only, payloadBits = 1263", "INVALID_BIT_LENGTH", decodeStatus(buf, bytes, 1263, true));
        addEq("E7d", "header only, payloadBits = 1264", "OK", decodeStatus(buf, bytes, 1264, true));
        addEq("E7e", "header, no payload, payloadBits = 1264", "INVALID_BIT_LENGTH", decodeStatus(buf, bytes, 1264, false));
        addEq("E7f", "payloadBits = size*8 + 1", "INVALID_BIT_LENGTH", decodeStatus(buf, bytes, bytes * 8 + 1, true));
    }
    printAndClear("payloadBits boundaries (E7)");

    // ---- E8: output buffer size -----------------------------------------------------------
    {
        auto tb = newBlock();
        tb->addLiteral('a'); tb->addLiteral('b'); tb->addLiteral('c');
        tb->addMatch(9, 3);
        tb->addLiteral('x');
        const w1::Plan p = w1::plan(*tb);
        const std::size_t need = p.totalBytes();
        const std::size_t caps[] = {need, need - 1, need - 2, need - 3, need - 4, need / 2, 0};
        for (std::size_t cap : caps) {
            std::vector<std::uint8_t> buf(cap ? cap : 1);
            w1::BitWriter w(cap ? buf.data() : nullptr, cap);
            const w1::Status s = w1::emit(*tb, p, w);
            w.finish();
            const bool detected = s != w1::Status::Ok || w.overflow();
            const bool mustBeOk = cap == need;
            const std::string actual = std::string("emit=") + w1::statusName(s) +
                                       ", overflow()=" + (w.overflow() ? "yes" : "no");
            add("E8", "buffer " + std::to_string(cap) + " of " + std::to_string(need) + " bytes",
                mustBeOk ? "OK, no overflow" : "detected",
                actual, mustBeOk ? (s == w1::Status::Ok && !w.overflow()) : detected);
        }
    }
    printAndClear("Output buffer size (E8): 'detected' = emit() fails OR overflow() is true after finish()");

    // ---- OAT: the 9 rows ----------------------------------------------------------------
    {
        const int L9[9][4] = {{1,1,1,1},{1,2,2,2},{1,3,3,3},{2,1,2,3},{2,2,3,1},{2,3,1,2},{3,1,3,2},{3,2,1,3},{3,3,2,1}};
        std::printf("\n== Orthogonal array rows: what each combination actually builds\n");
        std::printf("%-4s %-8s %7s %9s %9s %9s %10s %11s %s\n", "row", "A B C D", "words", "litSyms", "distSyms", "maxLit", "maxDist", "totalBytes", "lossless");
        for (int r = 0; r < 9; ++r) {
            auto tb = oatBlock(L9[r][0], L9[r][1], L9[r][2], L9[r][3], 1000u + L9[r][0]*27 + L9[r][1]*9 + L9[r][2]*3 + L9[r][3]);
            const w1::Plan p = w1::plan(*tb);
            int nl = 0, nd = 0, ml = 0, md = 0;
            for (unsigned i = 0; i < cmp::kNumLitLen; ++i) if (p.litLen[i]) { ++nl; if (p.litLen[i] > ml) ml = p.litLen[i]; }
            for (unsigned i = 0; i < cmp::kNumDist; ++i) if (p.distLen[i]) { ++nd; if (p.distLen[i] > md) md = p.distLen[i]; }
            const bool ok = lossless(*tb, p);
            if (!ok) ++failures;
            std::printf("%-4d %d %d %d %d    %7u %9d %9d %9d %10d %11zu %s\n", r + 1, L9[r][0], L9[r][1], L9[r][2], L9[r][3],
                        tb->n, nl, nd, ml, md, p.totalBytes(), ok ? "PASS" : "FAIL");
        }
    }

    std::printf("\n%s (%d failing row%s)\n", failures == 0 ? "ALL ROWS PASSED" : "SOME ROWS FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
