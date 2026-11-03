// huffman_types.h - plain data types used by the W1 API.
#pragma once

#include <cstddef>
#include <cstdint>

#include "symbols.hpp"

using namespace std;

namespace w1 {

enum class Status {
    Ok = 0,
    BufferOverflow,    // output buffer too small (use Plan::totalBytes() to size it)
    MissingCode,       // a token uses a symbol with no code: the frequency table is wrong
    InvalidToken,      // token field out of range (for example distance > 32768)
    PlanMismatch,      // bits written differ from the plan: frequencies do not match the tokens
    InvalidCodeTable,  // bad code lengths read from the stream
    InvalidBitLength,  // ran past payloadBits: truncated or corrupt payload
    InvalidCode,       // a bit pattern that matches no code
    TrailingBits,      // meaningful bits remain after EOB
};
const char* statusName(Status s) noexcept;

// The block header is the 286 + 30 code lengths, 4 bits each (0..15): 158 bytes exactly.
constexpr unsigned kHeaderBits = (cmp::kNumLitLen + cmp::kNumDist) * 4;

// Result of plan(): code lengths, canonical codes and the exact size, with no bits written yet.
struct Plan {
    Status status = Status::Ok;
    uint8_t  litLen[cmp::kNumLitLen] = {};
    uint8_t  distLen[cmp::kNumDist] = {};
    uint16_t litCode[cmp::kNumLitLen] = {};
    uint16_t distCode[cmp::kNumDist] = {};
    uint64_t headerBits = 0;    // code-length header
    uint64_t payloadBits = 0;   // Huffman codes + extra bits, including EOB
    uint64_t huffmanBits = 0;   // Huffman codes only (no extra bits, no header)
    uint64_t symbolCount = 0;   // Huffman symbols coded (litlen + distance, including EOB)

    uint64_t totalBits() const noexcept { return headerBits + payloadBits; }
    size_t totalBytes() const noexcept {
        return static_cast<size_t>((totalBits() + 7) / 8);
    }
};

// Decoder table for one alphabet: a 10-bit direct lookup for short codes, plus the canonical
// first/count/offset arrays for the rare longer ones.
constexpr unsigned kFastBits = 10;
struct DecodeTable {
    uint16_t fast[1u << kFastBits];            // (length << 9) | symbol, or 0 = not a short code
    uint16_t first[cmp::kMaxCodeLen + 1];      // first canonical code of each length
    uint16_t count[cmp::kMaxCodeLen + 1];      // number of codes of each length
    uint16_t offset[cmp::kMaxCodeLen + 1];     // index of the first symbol of each length in sorted[]
    uint16_t sorted[cmp::kNumLitLen];          // symbols ordered by (length, symbol)
    uint8_t maxLen;                            // 0 = empty table
};

struct HuffTables {
    DecodeTable lit;
    DecodeTable dist;
};

struct EntropyReport {
    double entropyBits = 0;               // Shannon bound for the coded symbols, in bits
    uint64_t huffmanBits = 0;        // bits Huffman actually used for the symbols (no extra bits)
    uint64_t symbols = 0;
    double entropyBitsPerSymbol = 0;
    double huffmanBitsPerSymbol = 0;
    double redundancyBitsPerSymbol = 0;   // huffman - entropy; always < 1 when no length limiting applied
};

}  // namespace w1