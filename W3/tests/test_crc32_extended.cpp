// test_crc32_extended.cpp
// Extended unit tests for w3::Crc32.
//
// The original test_crc32.cpp (not on this branch) covered the standard check
// vector, empty input, incremental updates, and reset().  This file fills the
// remaining gaps identified in the W3 unit-testing plan:
//
//   CRC-U-01  All-zeros 4-byte payload
//   CRC-U-02  All-0xFF 4-byte payload
//   CRC-U-03  Single byte 0x00
//   CRC-U-04  Single byte 0xFF
//   CRC-U-05  update() → reset() → update() (mid-use reset)
//   CRC-U-06  compute() is stateless (no hidden global state)
//   CRC-U-07  Byte-by-byte streaming matches one-shot
//   CRC-U-08  nullptr with size > 0 throws std::invalid_argument
//   CRC-U-09  Different data produces different CRC
//   CRC-U-10  Byte order matters (same bytes, reversed, different CRC)
//
// Reference values were verified with Python:  zlib.crc32(data) & 0xFFFFFFFF
//
// Build (standalone, no CMake needed for quick checks):
//   clang++ -std=c++17 -IW3/include W3/src/crc32.cpp \
//           W3/tests/test_crc32_extended.cpp -o test_crc32_ext && ./test_crc32_ext

#include "crc32.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using namespace std;
using w3::Crc32;

// ---------------------------------------------------------------------------
// Minimal test harness — no external dependencies
// ---------------------------------------------------------------------------
static int g_passed = 0;
static int g_failed = 0;

static void record_pass(const char* name) {
    cout << "  PASS  " << name << "\n";
    ++g_passed;
}

static void record_fail(const char* name, const char* detail) {
    cerr << "  FAIL  " << name << "  (" << detail << ")\n";
    ++g_failed;
}

#define REQUIRE(cond, id) \
    do { \
        if (cond) { record_pass(id); } \
        else      { record_fail(id, #cond); } \
    } while (false)

// Checks that the expression throws ExcType; fails the test otherwise.
template <typename ExcType, typename Fn>
static void require_throws(const char* id, Fn&& fn) {
    bool caught = false;
    try { fn(); }
    catch (const ExcType&) { caught = true; }
    catch (...) { }
    if (caught) record_pass(id);
    else        record_fail(id, "expected exception was not thrown");
}

// ---------------------------------------------------------------------------
// CRC-U-01  All-zeros 4-byte payload
// Reference: python3 -c "import zlib; print(hex(zlib.crc32(b'\\x00'*4)&0xFFFFFFFF))"
//            → 0x2144DF1C
// ---------------------------------------------------------------------------
static void test_all_zeros_payload() {
    cout << "\n[CRC-U-01] All-zeros payload\n";
    const array<uint8_t, 4> data = {0x00, 0x00, 0x00, 0x00};
    REQUIRE(Crc32::compute(data.data(), data.size()) == 0x2144DF1Cu,
            "CRC-U-01a: CRC32({0x00,0x00,0x00,0x00}) == 0x2144DF1C");

    // Also via incremental updates
    Crc32 crc;
    crc.update(data.data(), 2);
    crc.update(data.data() + 2, 2);
    REQUIRE(crc.value() == 0x2144DF1Cu,
            "CRC-U-01b: incremental all-zeros matches one-shot");
}

// ---------------------------------------------------------------------------
// CRC-U-02  All-0xFF 4-byte payload
// Reference: 0xFFFFFFFF
// ---------------------------------------------------------------------------
static void test_all_ff_payload() {
    cout << "\n[CRC-U-02] All-0xFF payload\n";
    const array<uint8_t, 4> data = {0xFF, 0xFF, 0xFF, 0xFF};
    const uint32_t result = Crc32::compute(data.data(), data.size());
    REQUIRE(result == 0xFFFFFFFFu,
            "CRC-U-02a: CRC32({0xFF*4}) == 0xFFFFFFFF");

    // Must be deterministic across two calls
    REQUIRE(Crc32::compute(data.data(), data.size()) == result,
            "CRC-U-02b: two identical calls return same value");
}

// ---------------------------------------------------------------------------
// CRC-U-03  Single byte 0x00
// Reference: 0xD202EF8D
// ---------------------------------------------------------------------------
static void test_single_zero_byte() {
    cout << "\n[CRC-U-03] Single byte 0x00\n";
    const uint8_t data = 0x00;
    REQUIRE(Crc32::compute(&data, 1) == 0xD202EF8Du,
            "CRC-U-03: CRC32({0x00}) == 0xD202EF8D");
}

// ---------------------------------------------------------------------------
// CRC-U-04  Single byte 0xFF
// Reference: 0xFF000000
// ---------------------------------------------------------------------------
static void test_single_ff_byte() {
    cout << "\n[CRC-U-04] Single byte 0xFF\n";
    const uint8_t data = 0xFF;
    REQUIRE(Crc32::compute(&data, 1) == 0xFF000000u,
            "CRC-U-04a: CRC32({0xFF}) == 0xFF000000");

    // Must differ from CRC of 0x00
    const uint8_t zero = 0x00;
    REQUIRE(Crc32::compute(&data, 1) != Crc32::compute(&zero, 1),
            "CRC-U-04b: CRC32(0xFF) != CRC32(0x00)");
}

// ---------------------------------------------------------------------------
// CRC-U-05  update() → reset() → update() — mid-use reset
// ---------------------------------------------------------------------------
static void test_reset_mid_use() {
    cout << "\n[CRC-U-05] Mid-use reset\n";
    constexpr array<uint8_t, 9> check = {'1','2','3','4','5','6','7','8','9'};
    const array<uint8_t, 3> abc = {'A', 'B', 'C'};

    Crc32 crc;
    // Feed something unrelated first
    crc.update(abc.data(), abc.size());
    const uint32_t before_reset = crc.value();
    REQUIRE(before_reset != 0u,
            "CRC-U-05a: value after ABC is non-zero");

    // Reset and verify the initial state
    crc.reset();
    REQUIRE(crc.value() == 0u,
            "CRC-U-05b: value immediately after reset() == 0");

    // Now feed the standard check vector; should produce the well-known result
    crc.update(check.data(), check.size());
    REQUIRE(crc.value() == 0xCBF43926u,
            "CRC-U-05c: re-use after reset produces correct check-vector CRC");
}

// ---------------------------------------------------------------------------
// CRC-U-06  compute() is stateless — no hidden global state between calls
// ---------------------------------------------------------------------------
static void test_compute_is_stateless() {
    cout << "\n[CRC-U-06] compute() is stateless\n";
    const array<uint8_t, 5> data = {0x01, 0x02, 0x03, 0x04, 0x05};

    // Reference: 0x470B99F4
    const uint32_t first  = Crc32::compute(data.data(), data.size());
    const uint32_t second = Crc32::compute(data.data(), data.size());
    const uint32_t third  = Crc32::compute(data.data(), data.size());

    REQUIRE(first == 0x470B99F4u,
            "CRC-U-06a: CRC32({1,2,3,4,5}) == 0x470B99F4");
    REQUIRE(first == second,
            "CRC-U-06b: 1st and 2nd call agree");
    REQUIRE(first == third,
            "CRC-U-06c: 3rd call still agrees (no leaked state)");
}

// ---------------------------------------------------------------------------
// CRC-U-07  Byte-by-byte streaming matches one-shot
// ---------------------------------------------------------------------------
static void test_byte_by_byte_streaming() {
    cout << "\n[CRC-U-07] Byte-by-byte streaming\n";
    constexpr array<uint8_t, 9> check = {'1','2','3','4','5','6','7','8','9'};

    Crc32 crc;
    for (size_t i = 0; i < check.size(); ++i) {
        crc.update(&check[i], 1);
    }
    REQUIRE(crc.value() == 0xCBF43926u,
            "CRC-U-07a: 9 × 1-byte updates == standard check vector");

    // Also try 2-byte chunks
    Crc32 crc2;
    crc2.update(check.data(), 2);
    crc2.update(check.data() + 2, 2);
    crc2.update(check.data() + 4, 2);
    crc2.update(check.data() + 6, 3);
    REQUIRE(crc2.value() == 0xCBF43926u,
            "CRC-U-07b: 2+2+2+3 byte chunks == standard check vector");
}

// ---------------------------------------------------------------------------
// CRC-U-08  nullptr with size > 0 throws std::invalid_argument
// ---------------------------------------------------------------------------
static void test_null_nonzero_throws() {
    cout << "\n[CRC-U-08] nullptr + size > 0 → invalid_argument\n";

    require_throws<invalid_argument>("CRC-U-08a: compute(nullptr,1)",
        [] { Crc32::compute(nullptr, 1); });

    require_throws<invalid_argument>("CRC-U-08b: compute(nullptr,100)",
        [] { Crc32::compute(nullptr, 100); });

    require_throws<invalid_argument>("CRC-U-08c: update(nullptr,1)",
        [] { Crc32 c; c.update(nullptr, 1); });

    require_throws<invalid_argument>("CRC-U-08d: update(nullptr,999)",
        [] { Crc32 c; c.update(nullptr, 999); });
}

// ---------------------------------------------------------------------------
// CRC-U-09  Different content → different CRC
// ---------------------------------------------------------------------------
static void test_different_data_different_crc() {
    cout << "\n[CRC-U-09] Different data → different CRC\n";
    const array<uint8_t, 4> a = {0x01, 0x02, 0x03, 0x04};
    const array<uint8_t, 4> b = {0x01, 0x02, 0x03, 0x05};  // last byte differs
    REQUIRE(Crc32::compute(a.data(), a.size()) != Crc32::compute(b.data(), b.size()),
            "CRC-U-09: 1-bit difference in data → different CRC");
}

// ---------------------------------------------------------------------------
// CRC-U-10  Byte order matters (same bytes, reversed)
// ---------------------------------------------------------------------------
static void test_byte_order_matters() {
    cout << "\n[CRC-U-10] Byte order matters\n";
    const array<uint8_t, 4> ab = {0xAA, 0xBB, 0x00, 0xFF};
    const array<uint8_t, 4> ba = {0xFF, 0x00, 0xBB, 0xAA};
    REQUIRE(Crc32::compute(ab.data(), ab.size()) != Crc32::compute(ba.data(), ba.size()),
            "CRC-U-10: reversed byte sequence produces different CRC");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    cout << "======================================================\n";
    cout << "  W3 Extended CRC-32 Unit Tests (test_crc32_extended)\n";
    cout << "======================================================\n";

    test_all_zeros_payload();
    test_all_ff_payload();
    test_single_zero_byte();
    test_single_ff_byte();
    test_reset_mid_use();
    test_compute_is_stateless();
    test_byte_by_byte_streaming();
    test_null_nonzero_throws();
    test_different_data_different_crc();
    test_byte_order_matters();

    cout << "\n------------------------------------------------------\n";
    cout << "  Passed: " << g_passed << "  |  Failed: " << g_failed << "\n";
    cout << "------------------------------------------------------\n";

    if (g_failed == 0) {
        cout << "  All CRC-32 extended tests PASSED.\n";
    } else {
        cerr << "  " << g_failed << " test(s) FAILED.\n";
    }
    return g_failed == 0 ? 0 : 1;
}

