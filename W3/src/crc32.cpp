#include "crc32.h"

#include <array>
#include <stdexcept>

using namespace std;

namespace w3 {
namespace {

constexpr uint32_t kReflectedPolynomial = 0xEDB88320u;

constexpr array<uint32_t, 256> makeTable() {
    array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < table.size(); ++i) {
        uint32_t crc = i;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1u) ^ kReflectedPolynomial): (crc >> 1u);
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto kTable = makeTable();

}  // namespace

void Crc32::update(const uint8_t* data, uint64_t size) {
    if (size == 0) return;
    if (data == nullptr) {
        throw invalid_argument("CRC32 input is null but has nonzero size");
    }
    for (uint64_t i = 0; i < size; ++i) {
        const auto index = static_cast<uint8_t>(state_ ^ data[i]);
        state_ = kTable[index] ^ (state_ >> 8u);
    }
}

uint32_t Crc32::value() const noexcept {
    return state_ ^ 0xFFFFFFFFu;
}

void Crc32::reset() noexcept {
    state_ = 0xFFFFFFFFu;
}

uint32_t Crc32::compute(const uint8_t* data, uint64_t size) {
    Crc32 crc;
    crc.update(data, size);
    return crc.value();
}

}  // namespace w3
