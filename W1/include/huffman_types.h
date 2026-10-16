// huffman_types.h - plain data types used by the W1 API.
#pragma once

#include <cstddef>
#include <cstdint>

#include "symbols.hpp"

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
    std::uint8_t  litLen[cmp::kNumLitLen] = {};
    std::uint8_t  distLen[cmp::kNumDist] = {};
    std::uint16_t litCode[cmp::kNumLitLen] = {};
    std::uint16_t distCode[cmp::kNumDist] = {};
    std::uint64_t headerBits = 0;    // code-length header
    std::uint64_t payloadBits = 0;   // Huffman codes + extra bits, including EOB
    std::uint64_t huffmanBits = 0;   // Huffman codes only (no extra bits, no header)
    std::uint64_t symbolCount = 0;   // Huffman symbols coded (litlen + distance, including EOB)

    std::uint64_t totalBits() const noexcept { return headerBits + payloadBits; }
    std::size_t totalBytes() const noexcept {
        return static_cast<std::size_t>((totalBits() + 7) / 8);
    }
};

// Decoder table for one alphabet: a 10-bit direct lookup for short codes, plus the canonical
// first/count/offset arrays for the rare longer ones.
constexpr unsigned kFastBits = 10;
struct DecodeTable {
    std::uint16_t fast[1u << kFastBits];            // (length << 9) | symbol, or 0 = not a short code
    std::uint16_t first[cmp::kMaxCodeLen + 1];      // first canonical code of each length
    std::uint16_t count[cmp::kMaxCodeLen + 1];      // number of codes of each length
    std::uint16_t offset[cmp::kMaxCodeLen + 1];     // index of the first symbol of each length in sorted[]
    std::uint16_t sorted[cmp::kNumLitLen];          // symbols ordered by (length, symbol)
    std::uint8_t maxLen;                            // 0 = empty table
};

struct HuffTables {
    DecodeTable lit;
    DecodeTable dist;
};

struct EntropyReport {
    double entropyBits = 0;               // Shannon bound for the coded symbols, in bits
    std::uint64_t huffmanBits = 0;        // bits Huffman actually used for the symbols (no extra bits)
    std::uint64_t symbols = 0;
    double entropyBitsPerSymbol = 0;
    double huffmanBitsPerSymbol = 0;
    double redundancyBitsPerSymbol = 0;   // huffman - entropy; always < 1 when no length limiting applied
};

}  // namespace w1