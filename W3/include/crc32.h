#pragma once

#include <cstdint>

namespace w3 {

using namespace std;

// CRC-32/IEEE (reflected polynomial 0xEDB88320).
// The caller supplies the exact payload bytes stored for one container block:
// compressed Huffman bytes or raw bytes for a stored block. CRC detects
// accidental corruption; it is not an authentication or security mechanism.
// update() may be called repeatedly with consecutive payload chunks.
class Crc32 {
public:
    Crc32() noexcept = default;

    void update(const uint8_t* data, uint64_t size);
    uint32_t value() const noexcept;
    void reset() noexcept;

    static uint32_t compute(const uint8_t* data, uint64_t size);

private:
    uint32_t state_ = 0xFFFFFFFFu;
};

}  // namespace w3
