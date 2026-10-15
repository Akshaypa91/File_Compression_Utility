#include "huffman.h"

#include <cmath>
#include <cstring>

namespace w1 {

const char* statusName(Status s) noexcept {
    switch (s) {
        case Status::Ok: return "OK";
        case Status::BufferOverflow: return "BUFFER_OVERFLOW";
        case Status::MissingCode: return "MISSING_CODE";
        case Status::InvalidToken: return "INVALID_TOKEN";
        case Status::PlanMismatch: return "PLAN_MISMATCH";
        case Status::InvalidCodeTable: return "INVALID_CODE_TABLE";
        case Status::InvalidBitLength: return "INVALID_BIT_LENGTH";
        case Status::InvalidCode: return "INVALID_CODE";
        case Status::TrailingBits: return "TRAILING_BITS";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// plan: frequencies (already counted by W2) -> lengths -> canonical codes -> exact size
// ---------------------------------------------------------------------------
Plan plan(const cmp::TokenBlock& block) {
    Plan p;

    std::uint32_t lf[cmp::kNumLitLen];
    std::memcpy(lf, block.litlenFreq, sizeof lf);
    if (lf[cmp::kEob] == 0) lf[cmp::kEob] = 1;      // EOB is always coded exactly once

    buildLengths(lf, cmp::kNumLitLen, cmp::kMaxCodeLen, p.litLen);
    buildLengths(block.distFreq, cmp::kNumDist, cmp::kMaxCodeLen, p.distLen);
    assignCanonical(p.litLen, cmp::kNumLitLen, p.litCode);
    assignCanonical(p.distLen, cmp::kNumDist, p.distCode);

    std::uint64_t bits = 0;
    std::uint64_t symbols = 0;
    for (unsigned i = 0; i < cmp::kNumLitLen; ++i) {
        bits += static_cast<std::uint64_t>(lf[i]) * p.litLen[i];
        symbols += lf[i];
    }
    for (unsigned i = 0; i < cmp::kNumDist; ++i) {
        bits += static_cast<std::uint64_t>(block.distFreq[i]) * p.distLen[i];
        symbols += block.distFreq[i];
    }
    p.huffmanBits = bits;
    p.payloadBits = bits + block.extraBitsTotal;
    p.headerBits = kHeaderBits;
    p.symbolCount = symbols;
    p.status = Status::Ok;
    return p;
}

// ---------------------------------------------------------------------------
// emit: header (code lengths) + tokens + EOB
// ---------------------------------------------------------------------------
Status emit(const cmp::TokenBlock& block, const Plan& p, BitWriter& out) {
    if (p.status != Status::Ok || block.n > cmp::kBlockTokens) return Status::InvalidToken;
    const std::uint64_t startBits = out.bitCount();

    for (unsigned i = 0; i < cmp::kNumLitLen; ++i) out.write(p.litLen[i], 4);
    for (unsigned i = 0; i < cmp::kNumDist; ++i) out.write(p.distLen[i], 4);

    const cmp::SymbolTables& tab = cmp::kTables;
    for (std::uint32_t i = 0; i < block.n; ++i) {
        const cmp::Tok t = block.tok[i];
        if (!cmp::isMatch(t)) {
            const unsigned b = cmp::litOf(t);
            const unsigned len = p.litLen[b];
            if (len == 0) return Status::MissingCode;
            out.write(p.litCode[b], len);
        } else {
            const std::uint32_t length = cmp::lenOf(t);
            const std::uint32_t distance = cmp::distOf(t);
            if (distance > cmp::kMaxDistance) return Status::InvalidToken;

            const unsigned ls = cmp::lenSymbol(length);
            const unsigned li = ls - cmp::kFirstLenSym;
            if (p.litLen[ls] == 0) return Status::MissingCode;
            out.write(p.litCode[ls], p.litLen[ls]);
            out.write(length - tab.lenBase[li], tab.lenExtra[li]);

            const unsigned ds = cmp::distSymbol(distance);
            if (p.distLen[ds] == 0) return Status::MissingCode;
            out.write(p.distCode[ds], p.distLen[ds]);
            out.write(distance - tab.distBase[ds], tab.distExtra[ds]);
        }
    }
    out.write(p.litCode[cmp::kEob], p.litLen[cmp::kEob]);

    if (out.overflow()) return Status::BufferOverflow;
    if (out.bitCount() - startBits != p.totalBits()) return Status::PlanMismatch;
    return Status::Ok;
}

// ---------------------------------------------------------------------------
// decoder tables
// ---------------------------------------------------------------------------
Status buildDecodeTable(const std::uint8_t* lens, unsigned n, DecodeTable& t) {
    std::memset(&t, 0, sizeof t);

    unsigned maxLen = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (lens[i] > cmp::kMaxCodeLen) return Status::InvalidCodeTable;
        if (lens[i] != 0) {
            ++t.count[lens[i]];
            if (lens[i] > maxLen) maxLen = lens[i];
        }
    }
    if (maxLen == 0) return Status::Ok;                       // empty table (no codes)
    if (kraftSum(lens, n) > (std::uint64_t{1} << cmp::kMaxCodeLen)) {
        return Status::InvalidCodeTable;                      // over-subscribed: not a prefix code
    }
    t.maxLen = static_cast<std::uint8_t>(maxLen);

    std::uint32_t code = 0;
    unsigned index = 0;
    for (unsigned len = 1; len <= cmp::kMaxCodeLen; ++len) {
        code = (code + t.count[len - 1]) << 1;
        t.first[len] = static_cast<std::uint16_t>(code);
        t.offset[len] = static_cast<std::uint16_t>(index);
        index += t.count[len];
    }

    unsigned fill[cmp::kMaxCodeLen + 1];
    for (unsigned len = 0; len <= cmp::kMaxCodeLen; ++len) fill[len] = t.offset[len];
    std::uint16_t codes[cmp::kNumLitLen];
    assignCanonical(lens, n, codes);
    for (unsigned s = 0; s < n; ++s) {
        const unsigned len = lens[s];
        if (len == 0) continue;
        t.sorted[fill[len]++] = static_cast<std::uint16_t>(s);
        if (len <= kFastBits) {
            const unsigned base = static_cast<unsigned>(codes[s]) << (kFastBits - len);
            const unsigned span = 1u << (kFastBits - len);
            for (unsigned k = 0; k < span; ++k) {
                t.fast[base + k] = static_cast<std::uint16_t>((len << 9) | s);
            }
        }
    }
    return Status::Ok;
}

Status readTables(BitReader& in, HuffTables& tables) {
    if (!in.valid() || in.bitsLeft() < kHeaderBits) return Status::InvalidBitLength;

    std::uint8_t litLen[cmp::kNumLitLen];
    std::uint8_t distLen[cmp::kNumDist];
    std::uint32_t v = 0;
    for (unsigned i = 0; i < cmp::kNumLitLen; ++i) {
        if (!in.readBits(4, v)) return Status::InvalidBitLength;
        litLen[i] = static_cast<std::uint8_t>(v);
    }
    for (unsigned i = 0; i < cmp::kNumDist; ++i) {
        if (!in.readBits(4, v)) return Status::InvalidBitLength;
        distLen[i] = static_cast<std::uint8_t>(v);
    }
    if (litLen[cmp::kEob] == 0) return Status::InvalidCodeTable;   // a block must be able to end

    Status s = buildDecodeTable(litLen, cmp::kNumLitLen, tables.lit);
    if (s != Status::Ok) return s;
    return buildDecodeTable(distLen, cmp::kNumDist, tables.dist);
}

// ---------------------------------------------------------------------------
// verification and reporting
// ---------------------------------------------------------------------------
bool verifyPrefixFree(const Plan& p) {
    return verifyPrefixFree(p.litLen, p.litCode, cmp::kNumLitLen) &&
           verifyPrefixFree(p.distLen, p.distCode, cmp::kNumDist);
}

EntropyReport entropyReport(const cmp::TokenBlock& block, const Plan& p) {
    EntropyReport r;

    std::uint32_t lf[cmp::kNumLitLen];
    std::memcpy(lf, block.litlenFreq, sizeof lf);
    if (lf[cmp::kEob] == 0) lf[cmp::kEob] = 1;       // same EOB rule as plan()

    // Entropy of each alphabet's own distribution, summed:  H = sum f * log2(total / f).
    auto accumulate = [&r](const std::uint32_t* f, unsigned n) {
        double total = 0;
        for (unsigned i = 0; i < n; ++i) total += f[i];
        if (total <= 0) return;
        for (unsigned i = 0; i < n; ++i) {
            if (f[i] > 0) r.entropyBits += f[i] * std::log2(total / f[i]);
        }
    };
    accumulate(lf, cmp::kNumLitLen);
    accumulate(block.distFreq, cmp::kNumDist);

    r.huffmanBits = p.huffmanBits;
    r.symbols = p.symbolCount;
    if (r.symbols > 0) {
        const double s = static_cast<double>(r.symbols);
        r.entropyBitsPerSymbol = r.entropyBits / s;
        r.huffmanBitsPerSymbol = static_cast<double>(r.huffmanBits) / s;
        r.redundancyBitsPerSymbol = r.huffmanBitsPerSymbol - r.entropyBitsPerSymbol;
    }
    return r;
}

}  // namespace w1
