// Tests for BitWriter / BitReader: exact bit patterns, padding, payloadBits, hard stop.
#include <random>
#include <string>
#include <vector>

#include "bit_io.h"
#include "test_util.h"

static std::string bitsOf(const std::vector<std::uint8_t>& bytes, std::uint64_t nbits) {
    std::string s;
    for (std::uint64_t i = 0; i < nbits; ++i) {
        s += ((bytes[i / 8] >> (7 - i % 8)) & 1) ? '1' : '0';
    }
    return s;
}

static void testExactPatterns() {
    // writing 101 then 11 gives 10111 and three padding zeros: 10111000
    std::vector<std::uint8_t> buf(4, 0xFF);
    w1::BitWriter w(buf.data(), buf.size());
    w.write(0b101, 3);
    w.write(0b11, 2);
    CHECK(w.bitCount() == 5);
    CHECK(w.finish() == 1);
    CHECK(buf[0] == 0b10111000);

    // the 13-bit example from the design doc: 1011011010101 -> B6 A8, bitLength 13
    std::vector<std::uint8_t> b2(4, 0xFF);
    w1::BitWriter w2(b2.data(), b2.size());
    w2.write(0b10110110, 8);
    w2.write(0b10101, 5);
    CHECK(w2.bitCount() == 13);
    CHECK(w2.finish() == 2);
    CHECK(b2[0] == 0xB6 && b2[1] == 0xA8);
    CHECK(bitsOf(b2, 13) == "1011011010101");

    // only the low nbits of the value are used
    std::vector<std::uint8_t> b3(4, 0);
    w1::BitWriter w3(b3.data(), b3.size());
    w3.write(0xFFFFFFFFu, 4);
    w3.write(0, 4);
    CHECK(w3.finish() == 1 && b3[0] == 0xF0);

    // exactly byte-aligned input needs no padding byte
    std::vector<std::uint8_t> b4(8, 0);
    w1::BitWriter w4(b4.data(), b4.size());
    w4.write(0xDEADBEEFu, 32);
    CHECK(w4.finish() == 4);
    CHECK(b4[0] == 0xDE && b4[1] == 0xAD && b4[2] == 0xBE && b4[3] == 0xEF);

    // nothing written -> nothing flushed
    std::vector<std::uint8_t> b5(1, 0);
    w1::BitWriter w5(b5.data(), b5.size());
    CHECK(w5.finish() == 0 && w5.bitCount() == 0);
}

static void testRoundTripRandom() {
    std::mt19937 rng(7);
    for (int round = 0; round < 300; ++round) {
        struct Item { std::uint32_t value; unsigned n; };
        std::vector<Item> items;
        std::uint64_t total = 0;
        const unsigned count = 1 + rng() % 400;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned n = rng() % 33;               // 0..32 bits, includes 0 and 32
            const std::uint32_t v = static_cast<std::uint32_t>(rng());
            items.push_back({n == 32 ? v : (v & ((1u << n) - 1)), n});
            total += n;
        }
        std::vector<std::uint8_t> buf(static_cast<std::size_t>((total + 7) / 8) + 4);
        w1::BitWriter w(buf.data(), buf.size());
        for (const Item& it : items) w.write(it.value, it.n);
        const std::size_t bytes = w.finish();
        CHECK(!w.overflow());
        CHECK(w.bitCount() == total);
        CHECK(bytes == (total + 7) / 8);

        w1::BitReader r(buf.data(), bytes, total);
        CHECK(r.valid());
        for (const Item& it : items) {
            std::uint32_t got = 0;
            CHECK(r.readBits(it.n, got));
            CHECK(got == it.value);
        }
        CHECK(r.bitsLeft() == 0);
        std::uint32_t extra = 0;
        CHECK(!r.readBits(1, extra));            // hard stop at payloadBits
    }
}

static void testHardStopIgnoresPadding() {
    // 13 meaningful bits stored in 2 bytes: the 3 padding bits must never be readable
    const std::uint8_t data[2] = {0xB6, 0xA8};
    w1::BitReader r(data, 2, 13);
    std::uint32_t v = 0;
    CHECK(r.readBits(8, v) && v == 0xB6);
    CHECK(r.readBits(5, v) && v == 0b10101);
    CHECK(r.bitsLeft() == 0);
    CHECK(!r.readBits(1, v));

    // asking for more than is left fails without consuming anything
    w1::BitReader r2(data, 2, 13);
    CHECK(!r2.readBits(14, v));
    CHECK(r2.bitsLeft() == 13);
    CHECK(r2.readBits(13, v) && v == 0b1011011010101);
}

static void testInvalidAndOverflow() {
    const std::uint8_t data[2] = {0, 0};
    w1::BitReader bad(data, 2, 17);          // payloadBits larger than the payload
    CHECK(!bad.valid());
    CHECK(bad.bitsLeft() == 16);

    std::vector<std::uint8_t> small(2, 0);
    w1::BitWriter w(small.data(), small.size());
    for (int i = 0; i < 100; ++i) w.write(0xFFFFFFFFu, 32);
    w.finish();
    CHECK(w.overflow());
    CHECK(w.bitCount() == 3200);              // bit count still correct after overflow
}

static void testReaderAcrossRefillBoundaries() {
    // long stream read with odd sizes to exercise both the 8-byte refill and the tail path
    std::mt19937 rng(99);
    std::vector<std::uint8_t> data(1000);
    for (auto& b : data) b = static_cast<std::uint8_t>(rng());
    const std::uint64_t total = data.size() * 8;

    std::string expected = bitsOf(data, total);
    w1::BitReader r(data.data(), data.size(), total);
    std::string got;
    while (r.bitsLeft() > 0) {
        unsigned n = 1 + rng() % 32;
        if (n > r.bitsLeft()) n = static_cast<unsigned>(r.bitsLeft());
        std::uint32_t v = 0;
        CHECK(r.readBits(n, v));
        for (int i = static_cast<int>(n) - 1; i >= 0; --i) got += ((v >> i) & 1) ? '1' : '0';
    }
    CHECK(got == expected);
}

int main() {
    testExactPatterns();
    testRoundTripRandom();
    testHardStopIgnoresPadding();
    testInvalidAndOverflow();
    testReaderAcrossRefillBoundaries();
    return finishTests("test_bit_io");
}
