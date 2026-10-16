// test_util.h - tiny test helpers shared by the W1 tests (no framework needed).
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "bit_io.h"
#include "huffman.h"
#include "symbols.hpp"

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failureCount();                                                    \
        }                                                                        \
    } while (0)

inline int finishTests(const char* name) {
    if (failureCount() == 0) {
        std::printf("%s: all tests passed\n", name);
        return 0;
    }
    std::printf("%s: %d CHECK(s) FAILED\n", name, failureCount());
    return 1;
}

// A sink that records exactly what the decoder produced, as tokens.
struct Recorder {
    std::vector<cmp::Tok> toks;
    void lit(std::uint8_t b) { toks.push_back(cmp::mkLit(b)); }
    bool match(std::uint32_t len, std::uint32_t dist) {
        toks.push_back(cmp::mkMatch(len, dist));
        return true;
    }
};

inline std::unique_ptr<cmp::TokenBlock> newBlock() {
    std::unique_ptr<cmp::TokenBlock> b(new cmp::TokenBlock);
    b->reset();
    return b;
}

// Random block generator. Modes:
//   0 uniform literals          1 literals + matches      2 matches only
//   3 skewed literals           4 one repeated literal    5 Fibonacci-skewed literals
//     (forces the 15-bit length limit to kick in)
inline void fillRandomBlock(cmp::TokenBlock& tb, std::mt19937& rng, int mode, unsigned count) {
    tb.reset();
    auto randLen = [&]() -> std::uint32_t {
        switch (rng() % 6) {
            case 0: return 3;
            case 1: return 258;
            case 2: return 257;
            default: return 3 + rng() % 256;
        }
    };
    auto randDist = [&]() -> std::uint32_t {
        switch (rng() % 8) {
            case 0: return 1;
            case 1: return 32768;
            case 2: return 256 + (rng() % 3);
            case 3: return 1 + rng() % 64;
            default: return 1 + rng() % 32768;
        }
    };
    if (mode == 5) {
        // Frequencies 1, 2, 3, 5, 8, ... for 20 symbols. Together with EOB (frequency 1) they
        // form the Fibonacci sequence, so every merge joins the previous node with the next
        // leaf and the natural Huffman tree is a 20-deep chain (about 28,600 tokens in total).
        std::uint64_t a = 1, b = 2;
        for (unsigned sym = 0; sym < 20 && !tb.full(); ++sym) {
            for (std::uint64_t k = 0; k < a && !tb.full(); ++k) tb.addLiteral(static_cast<std::uint8_t>(sym));
            const std::uint64_t c = a + b;
            a = b;
            b = c;
        }
        return;
    }
    for (unsigned i = 0; i < count && !tb.full(); ++i) {
        bool match = false;
        std::uint8_t lit = 0;
        switch (mode) {
            case 0: lit = static_cast<std::uint8_t>(rng()); break;
            case 1: match = (rng() & 1) != 0; lit = static_cast<std::uint8_t>(rng()); break;
            case 2: match = true; break;
            case 3: lit = (rng() % 10 < 8) ? 'e' : static_cast<std::uint8_t>(rng() % 20 + 'a'); break;
            case 4: lit = 'z'; break;
            default: break;
        }
        if (match) tb.addMatch(randLen(), randDist());
        else tb.addLiteral(lit);
    }
}

// plan -> emit -> readTables -> decodeBlock, returning the decoded tokens. Returns false on any
// failure (details in the checks).
inline bool roundTrip(const cmp::TokenBlock& tb, std::vector<cmp::Tok>& decoded, w1::Plan* planOut = nullptr) {
    w1::Plan p = w1::plan(tb);
    if (p.status != w1::Status::Ok) return false;
    if (planOut) *planOut = p;
    std::vector<std::uint8_t> buf(p.totalBytes());
    w1::BitWriter w(buf.data(), buf.size());
    if (w1::emit(tb, p, w) != w1::Status::Ok) return false;
    const std::size_t bytes = w.finish();
    if (bytes != buf.size() || w.overflow() || w.bitCount() != p.totalBits()) return false;

    w1::BitReader r(buf.data(), bytes, w.bitCount());
    std::unique_ptr<w1::HuffTables> t(new w1::HuffTables);
    if (w1::readTables(r, *t) != w1::Status::Ok) return false;
    Recorder rec;
    if (w1::decodeBlock(r, *t, rec) != w1::Status::Ok) return false;
    decoded = std::move(rec.toks);
    return true;
}