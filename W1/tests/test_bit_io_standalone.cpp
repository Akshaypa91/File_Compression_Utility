// Standalone tests for W1 BitWriter / BitReader.
// Contract: MSB-first bit order, zero-padded final byte, exact payload bit length.
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace std;
using namespace w1;

#include "bit_io.h"

namespace {
int failures = 0;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (false)

string toBits(const vector<uint8_t>& bytes, uint64_t bitCount) {
    string result;
    result.reserve(static_cast<size_t>(bitCount));
    for (uint64_t i = 0; i < bitCount; ++i) {
        const unsigned bit = (bytes[static_cast<size_t>(i / 8)] >> (7 - (i % 8))) & 1u;
        result.push_back(bit ? '1' : '0');
    }
    return result;
}

void testWriterExactPatterns() {
    {
        vector<uint8_t> bytes(2, 0xFF);
        BitWriter writer(bytes.data(), bytes.size());
        writer.write(0b101, 3);
        writer.write(0b11, 2);
        CHECK(writer.bitCount() == 5);
        CHECK(writer.finish() == 1);
        CHECK(bytes[0] == 0b10111000);
        CHECK(!writer.overflow());
    }
    {
        vector<uint8_t> bytes(2, 0);
        BitWriter writer(bytes.data(), bytes.size());
        writer.write(0b10110110, 8);
        writer.write(0b10101, 5);
        CHECK(writer.bitCount() == 13);
        CHECK(writer.finish() == 2);
        CHECK(bytes[0] == 0xB6 && bytes[1] == 0xA8);
        CHECK(toBits(bytes, 13) == "1011011010101");
    }
    {
        vector<uint8_t> bytes(4, 0);
        BitWriter writer(bytes.data(), bytes.size());
        writer.write(0xDEADBEEFu, 32);
        CHECK(writer.finish() == 4);
        CHECK(bytes[0] == 0xDE && bytes[1] == 0xAD);
        CHECK(bytes[2] == 0xBE && bytes[3] == 0xEF);
    }
    {
        vector<uint8_t> bytes(1, 0xFF);
        BitWriter writer(bytes.data(), bytes.size());
        writer.write(0xFFFFFFFFu, 4);
        writer.write(0u, 4);
        CHECK(writer.finish() == 1);
        CHECK(bytes[0] == 0xF0);
    }
    {
        vector<uint8_t> bytes(1, 0xA5);
        BitWriter writer(bytes.data(), bytes.size());
        writer.write(0u, 0);
        CHECK(writer.bitCount() == 0);
        CHECK(writer.finish() == 0);
        CHECK(bytes[0] == 0xA5);
    }
}

void testReaderExactAndHardStop() {
    const uint8_t payload[] = {0xB6, 0xA8};
    BitReader reader(payload, sizeof(payload), 13);
    CHECK(reader.valid());
    uint32_t value = 0;
    CHECK(reader.readBits(8, value) && value == 0xB6);
    CHECK(reader.readBits(5, value) && value == 0b10101);
    CHECK(reader.bitsLeft() == 0);
    CHECK(!reader.readBits(1, value));

    BitReader second(payload, sizeof(payload), 13);
    CHECK(!second.readBits(14, value));
    CHECK(second.bitsLeft() == 13);
    CHECK(second.readBits(13, value));
    CHECK(value == 0b1011011010101);
}

void testReaderInvalidLength() {
    const uint8_t payload[] = {0x12, 0x34};
    BitReader reader(payload, sizeof(payload), 17);
    CHECK(!reader.valid());
    CHECK(reader.bitsLeft() == 16);
}

void testWriterOverflow() {
    vector<uint8_t> bytes(2, 0);
    BitWriter writer(bytes.data(), bytes.size());
    for (int i = 0; i < 10; ++i) writer.write(0xFFFFFFFFu, 32);
    const size_t written = writer.finish();
    CHECK(writer.overflow());
    CHECK(writer.bitCount() == 320);
    CHECK(written <= bytes.size());
}

void testRandomRoundTrips() {
    mt19937 rng(0xC0FFEEu);
    for (int round = 0; round < 500; ++round) {
        struct Chunk { uint32_t value; unsigned bits; };
        vector<Chunk> chunks;
        uint64_t totalBits = 0;
        const unsigned count = 1 + (rng() % 250);

        for (unsigned i = 0; i < count; ++i) {
            const unsigned n = rng() % 33;
            const uint32_t value = static_cast<uint32_t>(rng());
            const uint32_t mask = (n == 32) ? 0xFFFFFFFFu :
                                       (n == 0) ? 0u : ((uint32_t{1} << n) - 1u);
            chunks.push_back({value & mask, n});
            totalBits += n;
        }

        const size_t capacity = static_cast<size_t>((totalBits + 7) / 8);
        vector<uint8_t> bytes(capacity + 8, 0xCC);
        BitWriter writer(bytes.data(), bytes.size());
        for (const Chunk& chunk : chunks) writer.write(chunk.value, chunk.bits);

        const size_t byteCount = writer.finish();
        CHECK(!writer.overflow());
        CHECK(writer.bitCount() == totalBits);
        CHECK(byteCount == capacity);

        BitReader reader(bytes.data(), byteCount, totalBits);
        CHECK(reader.valid());
        for (const Chunk& chunk : chunks) {
            uint32_t decoded = 0;
            CHECK(reader.readBits(chunk.bits, decoded));
            CHECK(decoded == chunk.value);
        }
        CHECK(reader.bitsLeft() == 0);
        uint32_t extra = 0;
        CHECK(!reader.readBits(1, extra));
    }
}

void testReaderAcrossRefillBoundaries() {
    mt19937 rng(123456u);
    vector<uint8_t> bytes(4097);
    for (uint8_t& byte : bytes) byte = static_cast<uint8_t>(rng());

    const uint64_t totalBits = bytes.size() * 8u;
    const string expected = toBits(bytes, totalBits);
    BitReader reader(bytes.data(), bytes.size(), totalBits);

    string actual;
    actual.reserve(expected.size());
    while (reader.bitsLeft() != 0) {
        unsigned n = 1 + (rng() % 32);
        if (n > reader.bitsLeft()) n = static_cast<unsigned>(reader.bitsLeft());

        uint32_t value = 0;
        CHECK(reader.readBits(n, value));
        for (int bit = static_cast<int>(n) - 1; bit >= 0; --bit) {
            actual.push_back(((value >> bit) & 1u) ? '1' : '0');
        }
    }
    CHECK(actual == expected);
    CHECK(reader.bitsLeft() == 0);
}
} // namespace

int main() {
    testWriterExactPatterns();
    testReaderExactAndHardStop();
    testReaderInvalidLength();
    testWriterOverflow();
    testRandomRoundTrips();
    testReaderAcrossRefillBoundaries();

    if (failures == 0) {
        printf("BitReader/BitWriter: PASS (%d checks)\\n", checks);
        return 0;
    }
    printf("BitReader/BitWriter: FAIL (%d/%d checks failed)\\n", failures, checks);
    return 1;
}
