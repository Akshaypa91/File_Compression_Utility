#include "crc32.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace std;

static void require(bool condition, const char* message) {
    if (!condition) {
        cerr << "FAIL: " << message << '\n';
        throw runtime_error(message);
    }
}

int main() {
    using w3::Crc32;

    // Standard CRC-32/IEEE check value (ASCII "123456789").
    constexpr array<uint8_t, 9> check = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    require(Crc32::compute(check.data(), check.size()) == 0xCBF43926u,
            "standard CRC-32/IEEE check vector");

    // Empty input and zero-length updates leave the checksum at zero.
    require(Crc32::compute(nullptr, 0) == 0u, "empty one-shot input");
    Crc32 empty;
    empty.update(nullptr, 0);
    require(empty.value() == 0u, "empty incremental input");

    bool rejectedNullRange = false;
    try {
        Crc32::compute(nullptr, 1);
    } catch (const invalid_argument&) {
        rejectedNullRange = true;
    }
    require(rejectedNullRange, "reject nonempty null input");

    // Incremental updates must equal a single pass, including empty chunks.
    Crc32 incremental;
    incremental.update(check.data(), 2);
    incremental.update(nullptr, 0);
    incremental.update(check.data() + 2, 3);
    incremental.update(check.data() + 5, 4);
    require(incremental.value() == Crc32::compute(check.data(), check.size()),
            "incremental updates match one-shot result");

    // Binary data is processed as bytes, including NUL and 0xFF.
    const array<uint8_t, 8> binary = {
        0x00, 0x1A, 0x80, 0xFF, 0x00, 0x7F, 0x10, 0xFE};
    const auto original = Crc32::compute(binary.data(), binary.size());
    require(original == 0x3D322D9Au, "binary-data reference vector");
    auto changed = binary;
    changed[4] ^= 0x01u;
    require(Crc32::compute(changed.data(), changed.size()) != original,
            "detect changed payload byte");

    // A realistic multi-chunk payload has the same result when streamed.
    vector<uint8_t> payload(1u << 20);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>((i * 37u) & 0xFFu);
    }
    const auto onePass = Crc32::compute(payload.data(), payload.size());
    require(onePass == 0x64E0C2A4u, "large-payload reference vector");
    Crc32 streamed;
    constexpr size_t chunk = 8191;
    for (size_t offset = 0; offset < payload.size(); offset += chunk) {
        const auto count = payload.size() - offset < chunk
                               ? payload.size() - offset
                               : chunk;
        streamed.update(payload.data() + offset, count);
    }
    require(streamed.value() == onePass,
            "many chunks match the one-shot result");

    // reset() starts a new independent checksum.
    incremental.reset();
    require(incremental.value() == 0u, "reset restores initial checksum");
    incremental.update(check.data(), check.size());
    require(incremental.value() == 0xCBF43926u,
            "reset instance computes new checksum");
    cout << "All CRC32 checks passed.\n";
}
