// huffman.h - the public W1 API. W3 calls these; W2 never does.
//
//   Compress:    plan(block)  ->  [W3 chooses stored or compressed]  ->  emit(block, plan, writer)
//   Decompress:  readTables(reader)  ->  decodeBlock(reader, tables, sink)
//
// W1 knows nothing about LZ77: it only sees symbols, frequencies and extra bits.
#pragma once

#include <cstdint>

#include "bit_io.h"
#include "huffman_tree.h"
#include "huffman_types.h"
#include "symbols.hpp"

namespace w1 {

// Builds the two Huffman codes for a block from the frequencies W2 already counted, and
// computes the exact size. Writes no bits. Check Plan::status.
Plan plan(const cmp::TokenBlock& block);

// Writes the code-length header, every token (Huffman code + extra bits) and EOB into `out`.
// Returns BufferOverflow if `out` is too small and PlanMismatch if the block's frequencies
// did not match its tokens. Use `out.bitCount()` as the block's payloadBits.
Status emit(const cmp::TokenBlock& block, const Plan& p, BitWriter& out);

// Reads the code-length header and builds the decoder tables.
Status readTables(BitReader& in, HuffTables& tables);

// Builds a decoder table from code lengths (used by readTables; exposed for tests).
Status buildDecodeTable(const std::uint8_t* lens, unsigned n, DecodeTable& table);

// Entropy comparison for the report: Shannon bound vs the bits Huffman actually used.
EntropyReport entropyReport(const cmp::TokenBlock& block, const Plan& p);

// Explicit prefix-free check of both codes in a plan.
bool verifyPrefixFree(const Plan& p);

// Decodes one symbol. Returns InvalidBitLength if the code would run past payloadBits.
inline Status decodeSymbol(const DecodeTable& t, BitReader& br, std::uint32_t& sym) noexcept {
    const std::uint16_t e = t.fast[br.peek(kFastBits)];
    if (e != 0) {
        const unsigned len = static_cast<unsigned>(e >> 9);
        if (len > br.bitsLeft()) return Status::InvalidBitLength;
        br.consume(len);
        sym = e & 0x1FFu;
        return Status::Ok;
    }
    for (unsigned len = kFastBits + 1; len <= t.maxLen; ++len) {
        const std::uint32_t code = br.peek(len);
        if (code >= t.first[len] && code - t.first[len] < t.count[len]) {
            if (len > br.bitsLeft()) return Status::InvalidBitLength;
            br.consume(len);
            sym = t.sorted[t.offset[len] + (code - t.first[len])];
            return Status::Ok;
        }
    }
    return Status::InvalidCode;
}

// Decodes one block until EOB, pushing results straight into `out` (no intermediate buffer).
//   Sink must provide:   void lit(std::uint8_t byte);
//                        bool match(std::uint32_t length, std::uint32_t distance);
// `match` returns false to reject an impossible reference (for example a distance greater
// than the bytes produced so far); decoding then stops with Status::InvalidToken.
// After EOB, no meaningful bits may remain (otherwise TrailingBits).
template <class Sink>
Status decodeBlock(BitReader& br, const HuffTables& t, Sink& out) {
    for (;;) {
        br.refill();                      // >= 56 bits: enough for one whole match token
        std::uint32_t sym = 0;
        Status s = decodeSymbol(t.lit, br, sym);
        if (s != Status::Ok) return s;
        if (sym < cmp::kEob) {
            out.lit(static_cast<std::uint8_t>(sym));
            continue;
        }
        if (sym == cmp::kEob) break;

        const unsigned li = sym - cmp::kFirstLenSym;      // sym <= 285, so li <= 28
        std::uint32_t extra = 0;
        if (!br.readBits(cmp::kTables.lenExtra[li], extra)) return Status::InvalidBitLength;
        const std::uint32_t length = cmp::kTables.lenBase[li] + extra;

        if (t.dist.maxLen == 0) return Status::InvalidCodeTable;   // a match but no distance codes
        std::uint32_t ds = 0;
        s = decodeSymbol(t.dist, br, ds);
        if (s != Status::Ok) return s;
        if (!br.readBits(cmp::kTables.distExtra[ds], extra)) return Status::InvalidBitLength;
        const std::uint32_t distance = cmp::kTables.distBase[ds] + extra;

        if (!out.match(length, distance)) return Status::InvalidToken;
    }
    return br.bitsLeft() == 0 ? Status::Ok : Status::TrailingBits;
}

}  // namespace w1
