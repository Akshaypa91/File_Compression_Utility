// symbols.hpp - the shared contract between W1 (Huffman), W2 (LZ77) and W3 (pipeline).
//
// PROPOSED: this file must be reviewed and frozen by all three worklet leads.
// Nobody changes it alone. Any change needs the change note described in the README.
//
// Alphabet (DEFLATE-style):
//   literal/length : 286 symbols   0..255 literal byte, 256 = EOB, 257..285 length buckets
//   distance       :  30 symbols   each is a distance bucket
// A match is (length 3..258, distance 1..32768). Length and distance are split into a
// bucket symbol (Huffman coded) plus "extra bits" (raw bits, written MSB-first).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cmp {

constexpr unsigned kNumLitLen   = 286;
constexpr unsigned kNumDist     = 30;
constexpr unsigned kNumLenCodes = 29;       // length buckets 257..285
constexpr unsigned kEob         = 256;
constexpr unsigned kFirstLenSym = 257;
constexpr unsigned kMinMatch    = 3;
constexpr unsigned kMaxMatch    = 258;
constexpr unsigned kMaxDistance = 32768;
constexpr unsigned kMaxCodeLen  = 15;       // Huffman code lengths are limited to 15 bits
constexpr unsigned kBlockTokens = 32768;    // tokens per block (EOB is not stored in tok[])

// ---------------------------------------------------------------------------
// Token word: one 32-bit value per token.
//   literal : bit31 = 0, low 8 bits = the byte
//   match   : bit31 = 1, bits 16..23 = (length - 3), bits 0..15 = (distance - 1)
// ---------------------------------------------------------------------------
using Tok = std::uint32_t;
constexpr Tok kMatchFlag = 0x80000000u;

constexpr Tok mkLit(std::uint8_t b) { return b; }
constexpr Tok mkMatch(std::uint32_t len, std::uint32_t dist) {
    return kMatchFlag | ((len - kMinMatch) << 16) | (dist - 1);
}
constexpr bool isMatch(Tok t) { return (t & kMatchFlag) != 0; }
constexpr std::uint8_t litOf(Tok t) { return static_cast<std::uint8_t>(t & 0xFFu); }
constexpr std::uint32_t lenOf(Tok t) { return ((t >> 16) & 0xFFu) + kMinMatch; }
constexpr std::uint32_t distOf(Tok t) { return (t & 0xFFFFu) + 1; }

// ---------------------------------------------------------------------------
// Bucket tables (the standard DEFLATE tables, generated at compile time).
// W1 and W2 both include these; neither re-implements them.
// ---------------------------------------------------------------------------
struct SymbolTables {
    std::uint16_t lenBase[kNumLenCodes];
    std::uint8_t  lenExtra[kNumLenCodes];
    std::uint16_t distBase[kNumDist];
    std::uint8_t  distExtra[kNumDist];
    std::uint8_t  lenCode[256];     // (length - 3) -> bucket index 0..28
    std::uint8_t  distLow[256];     // (distance - 1) < 256      -> bucket index
    std::uint8_t  distHigh[256];    // (distance - 1) >> 7, for (distance - 1) >= 256

    constexpr SymbolTables()
        : lenBase{3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                  35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258},
          lenExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                   3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0},
          distBase{1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                   257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                   8193, 12289, 16385, 24577},
          distExtra{0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13},
          lenCode{}, distLow{}, distHigh{} {
        // Buckets 0..27 cover base .. base + 2^extra - 1; length 258 is bucket 28 only.
        for (unsigned c = 0; c + 1 < kNumLenCodes; ++c) {
            const unsigned hi = lenBase[c] + (1u << lenExtra[c]) - 1;
            for (unsigned l = lenBase[c]; l <= hi; ++l) {
                lenCode[l - kMinMatch] = static_cast<std::uint8_t>(c);
            }
        }
        lenCode[kMaxMatch - kMinMatch] = static_cast<std::uint8_t>(kNumLenCodes - 1);

        for (unsigned c = 0; c < kNumDist; ++c) {
            const unsigned lo = distBase[c] - 1u;
            const unsigned hi = lo + (1u << distExtra[c]) - 1u;
            for (unsigned z = lo; z <= hi && z < 256; ++z) {
                distLow[z] = static_cast<std::uint8_t>(c);
            }
            if (hi >= 256) {
                for (unsigned i = (lo < 256 ? 256u : lo) >> 7; i <= (hi >> 7); ++i) {
                    distHigh[i] = static_cast<std::uint8_t>(c);
                }
            }
        }
    }
};

inline constexpr SymbolTables kTables{};

// literal/length symbol (257..285) for a match length 3..258
constexpr unsigned lenSymbol(std::uint32_t len) {
    return kFirstLenSym + kTables.lenCode[len - kMinMatch];
}
// distance symbol (0..29) for a distance 1..32768
constexpr unsigned distSymbol(std::uint32_t dist) {
    return (dist - 1) < 256 ? kTables.distLow[dist - 1] : kTables.distHigh[(dist - 1) >> 7];
}

// ---------------------------------------------------------------------------
// Project token model (HLD/LLD section 6.1): Token{offset, length, nextChar}.
//   literal token : offset = 0, length = 0, nextChar = the byte
//   match token   : copy `length` bytes from `offset` back, then output `nextChar`
// W2's public output is this triple. TokenBlock::addToken() stores it as the compact words
// above (a match word followed by a literal word), so W1 and the frequency counting are
// unchanged. End-of-input rule: a match must never reach the last byte of its chunk; W2
// shortens it so that nextChar always exists (no special end marker is needed).
// ---------------------------------------------------------------------------
struct Token {
    std::uint32_t offset;
    std::uint32_t length;
    std::uint8_t  nextChar;
};

// ---------------------------------------------------------------------------
// TokenBlock: the shared box W2 fills and W1 reads. W3 allocates it once and reuses it.
// W2 counts frequencies while emitting, so W1 never needs a counting pass.
// ---------------------------------------------------------------------------
struct TokenBlock {
    Tok           tok[kBlockTokens];
    std::uint32_t n;                        // tokens used
    std::uint32_t litlenFreq[kNumLitLen];   // includes litlenFreq[kEob] == 1
    std::uint32_t distFreq[kNumDist];
    std::uint64_t extraBitsTotal;           // sum of extra bits over all matches
    const std::uint8_t* src;                // raw bytes this block covers (stored fallback)
    std::uint32_t srcLen;

    void reset() {
        n = 0;
        std::memset(litlenFreq, 0, sizeof litlenFreq);
        std::memset(distFreq, 0, sizeof distFreq);
        litlenFreq[kEob] = 1;
        extraBitsTotal = 0;
        src = nullptr;
        srcLen = 0;
    }
    bool full() const { return n >= kBlockTokens; }

    // Helpers that keep tokens and counters consistent. W2 should use these.
    void addLiteral(std::uint8_t b) {
        tok[n++] = mkLit(b);
        ++litlenFreq[b];
    }
    void addMatch(std::uint32_t len, std::uint32_t dist) {
        tok[n++] = mkMatch(len, dist);
        const unsigned ls = lenSymbol(len);
        const unsigned ds = distSymbol(dist);
        ++litlenFreq[ls];
        ++distFreq[ds];
        extraBitsTotal += kTables.lenExtra[ls - kFirstLenSym] + kTables.distExtra[ds];
    }

    // True when one more Token{offset,length,nextChar} is guaranteed to fit (it needs 2 words).
    bool hasRoomForToken() const { return n + 2 <= kBlockTokens; }

    // Stores one project Token and updates the counts. Precondition: hasRoomForToken(), and
    // either length == 0 (literal) or kMinMatch <= length <= kMaxMatch and
    // 1 <= offset <= kMaxDistance.
    void addToken(const Token& t) {
        if (t.length != 0) addMatch(t.length, t.offset);
        addLiteral(t.nextChar);
    }
};

}  // namespace cmp