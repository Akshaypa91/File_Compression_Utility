// demo.cpp - walks the golden example through W1 and prints every step.
// Build it with CMake and run:  ./w1_demo
// This is the same vector the tests check, so it is the thing to explain in the viva.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "huffman.h"

static std::string binary(unsigned value, unsigned bits) {
    std::string s;
    for (int i = static_cast<int>(bits) - 1; i >= 0; --i) s += ((value >> i) & 1) ? '1' : '0';
    return s;
}

struct Printer {
    std::string text;
    void lit(std::uint8_t b) { text += static_cast<char>(b); }
    bool match(std::uint32_t len, std::uint32_t dist) {
        std::printf("    decoder: copy %u bytes from %u back\n", len, dist);
        for (std::uint32_t i = 0; i < len; ++i) text += text[text.size() - dist];   // byte by byte (overlap-safe)
        return dist <= text.size();
    }
};

int main() {
    std::printf("input: abcabcabcabcx (13 bytes)\n\n");

    std::unique_ptr<cmp::TokenBlock> tb(new cmp::TokenBlock);
    tb->reset();
    tb->addLiteral('a');
    tb->addLiteral('b');
    tb->addLiteral('c');
    tb->addMatch(9, 3);
    tb->addLiteral('x');

    std::printf("1. tokens from W2: lit a, lit b, lit c, match(len 9, dist 3), lit x\n");
    std::printf("   symbols: len 9 -> %u, dist 3 -> %u, EOB -> %u\n\n",
                cmp::lenSymbol(9), cmp::distSymbol(3), cmp::kEob);

    const w1::Plan p = w1::plan(*tb);
    std::printf("2. plan (no bits written yet)\n");
    for (unsigned s = 0; s < cmp::kNumLitLen; ++s) {
        if (p.litLen[s]) {
            std::printf("   litlen %3u  freq %u  length %u  code %s\n", s, tb->litlenFreq[s],
                        p.litLen[s], binary(p.litCode[s], p.litLen[s]).c_str());
        }
    }
    for (unsigned s = 0; s < cmp::kNumDist; ++s) {
        if (p.distLen[s]) {
            std::printf("   dist   %3u  freq %u  length %u  code %s\n", s, tb->distFreq[s],
                        p.distLen[s], binary(p.distCode[s], p.distLen[s]).c_str());
        }
    }
    std::printf("   header %llu bits, payload %llu bits, prefix-free: %s\n\n",
                static_cast<unsigned long long>(p.headerBits),
                static_cast<unsigned long long>(p.payloadBits),
                w1::verifyPrefixFree(p) ? "yes" : "NO");

    std::vector<std::uint8_t> buf(p.totalBytes());
    w1::BitWriter w(buf.data(), buf.size());
    w1::emit(*tb, p, w);
    w.finish();
    std::printf("3. emitted: %llu meaningful bits in %zu bytes\n", static_cast<unsigned long long>(w.bitCount()), buf.size());
    std::printf("   payload bytes after the %u-byte header: %02X %02X %02X   (17 real bits + 7 padding zeros)\n\n",
                w1::kHeaderBits / 8, buf[158], buf[159], buf[160]);

    const w1::EntropyReport e = w1::entropyReport(*tb, p);
    std::printf("4. entropy: %.3f bits/symbol, Huffman used %.3f bits/symbol (redundancy %.3f)\n\n",
                e.entropyBitsPerSymbol, e.huffmanBitsPerSymbol, e.redundancyBitsPerSymbol);

    std::printf("5. decode\n");
    w1::BitReader r(buf.data(), buf.size(), w.bitCount());
    std::unique_ptr<w1::HuffTables> t(new w1::HuffTables);
    w1::readTables(r, *t);
    Printer out;
    const w1::Status s = w1::decodeBlock(r, *t, out);
    std::printf("   status: %s\n   output: %s\n", w1::statusName(s), out.text.c_str());
    return out.text == "abcabcabcabcx" && s == w1::Status::Ok ? 0 : 1;
}