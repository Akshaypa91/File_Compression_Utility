// test_container_extended.cpp
// Extended unit tests for cmp2::ContainerWriter and cmp2::ContainerReader.
//
// The original test_container_format.cpp covered the basic round-trip, empty
// file, streaming API, and several error codes.  This file fills the remaining
// gaps from the W3 unit-testing plan:
//
// ContainerWriter gaps (CW-U-xx):
//   CW-U-01  Write a block after finish() -> InvalidBlockOrder
//   CW-U-02  Write a second block after the final block -> InvalidBlockOrder
//   CW-U-03  Empty payload vector but payloadBits > 0 -> InvalidArgument
//   CW-U-04  sourceLength > kMaxSourceBytesPerBlock -> InvalidBlock
//   CW-U-05  payloadBits > payloadBytes*8 -> InvalidBitLength
//   CW-U-06  Compressed block with sourceLength = 0 -> InvalidBlock
//   CW-U-07  log2Window > 15 on open() -> InvalidWindow
//   CW-U-08  Raw header bytes verify magic, version, log2Window
//   CW-U-09  Three-block file: only last has isLast flag
//   CW-U-10  CRC field is stored verbatim (container does NOT recompute it)
//   CW-U-11  Large sourceLength (varint > 1 byte) is preserved exactly
//
// ContainerReader gaps (CR-U-xx):
//   CR-U-01  Open a nonexistent file -> FileOpenFailed
//   CR-U-02  log2Window byte = 16 -> InvalidWindow
//   CR-U-03  BlockType bits = 2 (invalid) in flags byte -> InvalidBlockType
//   CR-U-04  Reserved flag bits set -> InvalidFlags
//   CR-U-05  readNextBlock() after the final block -> InvalidBlockOrder
//   CR-U-06  finish() called when no final block was read -> MissingFinalBlock
//   CR-U-07  Stored block: sourceLength != payloadBytes in file -> InvalidBlock
//   CR-U-08  Non-canonical varint (0x80 0x00 encoding of 0) -> CorruptHeader
//   CR-U-09  Unterminated varint (10 continuation bytes) -> CorruptHeader
//   CR-U-10  CRC value 0x00000000 round-trips correctly
//   CR-U-11  CRC value 0xFFFFFFFF round-trips correctly
//   CR-U-12  hasNextBlock() returns false after the final block is read

#include "container_format.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

using cmp2::BlockType;
using cmp2::ContainerBlock;
using cmp2::ContainerError;
using cmp2::ErrorCode;

// ---------------------------------------------------------------------------
// Test infrastructure
// ---------------------------------------------------------------------------
static int g_passed = 0;
static int g_failed = 0;

static void record_pass(const char* id) {
    std::cout << "  PASS  " << id << "\n";
    ++g_passed;
}
static void record_fail(const char* id, const char* detail) {
    std::cerr << "  FAIL  " << id << "  (" << detail << ")\n";
    ++g_failed;
}

#define REQUIRE(cond, id) \
    do { \
        if (cond) { record_pass(id); } \
        else      { record_fail(id, #cond); } \
    } while (false)

// Asserts that fn() throws ContainerError with the given ErrorCode.
static void expect_error(ErrorCode expected, const char* id,
                         const std::function<void()>& fn) {
    bool caught = false;
    ErrorCode got = ErrorCode::FileOpenFailed;  // placeholder
    try { fn(); }
    catch (const ContainerError& e) { caught = true; got = e.code(); }
    catch (...) {}

    if (!caught) {
        record_fail(id, "ContainerError not thrown");
    } else if (got != expected) {
        std::string detail = std::string("wrong code: got ")
                           + cmp2::error_code_name(got)
                           + " expected "
                           + cmp2::error_code_name(expected);
        record_fail(id, detail.c_str());
    } else {
        record_pass(id);
    }
}

// ---------------------------------------------------------------------------
// Temporary file helpers
// ---------------------------------------------------------------------------
const std::filesystem::path kTmpDir =
    std::filesystem::temp_directory_path() / "cmp2_ext_tests";

static std::string tp(const std::string& name) {
    return (kTmpDir / name).string();
}

static void write_bytes(const std::string& path,
                        const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out);
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    assert(out);
}

static std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    assert(in);
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    if (sz) in.read(reinterpret_cast<char*>(buf.data()),
                    static_cast<std::streamsize>(sz));
    return buf;
}

// ---------------------------------------------------------------------------
// Block-building helpers (mirror those in test_container_format.cpp)
// ---------------------------------------------------------------------------
static ContainerBlock make_stored(bool last = true) {
    ContainerBlock b;
    b.type        = BlockType::Stored;
    b.isLast      = last;
    b.payload     = {0xDE, 0xAD, 0xBE, 0xEF};
    b.sourceLength = b.payload.size();
    b.payloadBits  = b.payload.size() * 8ULL;
    b.crc32        = 0xABCD1234U;
    return b;
}

static ContainerBlock make_compressed(bool last = true) {
    ContainerBlock b;
    b.type        = BlockType::Compressed;
    b.isLast      = last;
    b.sourceLength = 42;
    b.payload     = {0x1A, 0x2B};
    b.payloadBits  = 11;
    b.crc32        = 0x11223344U;
    return b;
}
// suppress unused-function warning for make_compressed
static const auto& _use_make_compressed = make_compressed;

// Write a minimal valid single-stored-block archive and return the path.
static std::string make_valid_archive(const std::string& name,
                                      std::uint8_t log2Win = 15) {
    const std::string path = tp(name);
    cmp2::write_container(path, log2Win, {make_stored(true)});
    return path;
}

// ===========================================================================
// ContainerWriter tests
// ===========================================================================

// CW-U-01: Write a block after finish() must throw InvalidBlockOrder.
static void test_write_after_finish() {
    std::cout << "\n[CW-U-01] Write after finish()\n";
    const std::string path = tp("cw_write_after_finish.cmp2");
    expect_error(ErrorCode::InvalidBlockOrder, "CW-U-01", [&] {
        cmp2::ContainerWriter w = cmp2::ContainerWriter::open(path, 15);
        w.writeBlock(make_stored(true));
        w.finish();
        w.writeBlock(make_stored(true));  // must throw
    });
}

// CW-U-02: Write another block after the final block (isLast=true) -> InvalidBlockOrder.
static void test_write_after_last_block() {
    std::cout << "\n[CW-U-02] Write after last block\n";
    const std::string path = tp("cw_write_after_last.cmp2");
    expect_error(ErrorCode::InvalidBlockOrder, "CW-U-02", [&] {
        cmp2::ContainerWriter w = cmp2::ContainerWriter::open(path, 15);
        w.writeBlock(make_stored(true));  // isLast = true
        w.writeBlock(make_stored(true));  // must throw
    });
}

// CW-U-03: Empty payload vector but payloadBits > 0 -> InvalidBitLength.
// validate_block_for_write checks (payloadBits > payloadBytes*8) before the
// Stored-specific checks.  An empty payload (payloadBytes=0) with payloadBits=32
// violates 32 > 0*8, so InvalidBitLength is raised.
static void test_null_payload_with_nonzero_size() {
    std::cout << "\n[CW-U-03] Empty payload vector but payloadBits > 0\n";
    const std::string path = tp("cw_null_payload.cmp2");
    expect_error(ErrorCode::InvalidBitLength, "CW-U-03", [&] {
        cmp2::ContainerWriter w = cmp2::ContainerWriter::open(path, 15);
        ContainerBlock b;
        b.type         = BlockType::Stored;
        b.isLast       = true;
        b.sourceLength = 4;
        b.payloadBits  = 32;  // claims 32 bits but payload vector is empty
        b.payload      = {};  // empty -- inconsistent with payloadBits > 0
        b.crc32        = 0;
        w.writeBlock(b);
    });
}

// CW-U-04: sourceLength > kMaxSourceBytesPerBlock -> InvalidBlock.
static void test_source_length_exceeds_limit() {
    std::cout << "\n[CW-U-04] sourceLength > per-block limit\n";
    const std::string path = tp("cw_big_source.cmp2");
    expect_error(ErrorCode::InvalidBlock, "CW-U-04", [&] {
        cmp2::ContainerWriter w = cmp2::ContainerWriter::open(path, 15);
        ContainerBlock b;
        b.type         = BlockType::Stored;
        b.isLast       = true;
        b.sourceLength = cmp2::kMaxSourceBytesPerBlock + 1;  // too large
        b.payload      = {0x00};
        b.payloadBits  = 8;
        b.crc32        = 0;
        w.writeBlock(b);
    });
}

// CW-U-05: payloadBits > payloadBytes * 8 -> InvalidBitLength.
static void test_payload_bits_exceeds_bytes() {
    std::cout << "\n[CW-U-05] payloadBits > payloadBytes*8\n";
    const std::string path = tp("cw_bad_bits.cmp2");
    expect_error(ErrorCode::InvalidBitLength, "CW-U-05", [&] {
        cmp2::ContainerWriter w = cmp2::ContainerWriter::open(path, 15);
        ContainerBlock b;
        b.type         = BlockType::Compressed;
        b.isLast       = true;
        b.sourceLength = 10;
        b.payload      = {0x00, 0x00};  // 2 bytes = 16 bits max
        b.payloadBits  = 17;            // 17 > 16 -- invalid
        b.crc32        = 0;
        w.writeBlock(b);
    });
}

// CW-U-06: Compressed block with sourceLength = 0 -> InvalidBlock.
static void test_compressed_zero_source_length() {
    std::cout << "\n[CW-U-06] Compressed block with sourceLength = 0\n";
    const std::string path = tp("cw_zero_source.cmp2");
    expect_error(ErrorCode::InvalidBlock, "CW-U-06", [&] {
        cmp2::ContainerWriter w = cmp2::ContainerWriter::open(path, 15);
        ContainerBlock b;
        b.type         = BlockType::Compressed;
        b.isLast       = true;
        b.sourceLength = 0;    // invalid for compressed block
        b.payload      = {0xAA};
        b.payloadBits  = 8;
        b.crc32        = 0;
        w.writeBlock(b);
    });
}

// CW-U-07: log2Window > 15 on open() -> InvalidWindow.
static void test_open_invalid_window() {
    std::cout << "\n[CW-U-07] log2Window > 15 on open()\n";
    expect_error(ErrorCode::InvalidWindow, "CW-U-07", [&] {
        cmp2::ContainerWriter::open(tp("cw_bad_window.cmp2"), 16);
    });
}

// CW-U-08: Verify raw header bytes: magic, version, log2Window.
static void test_raw_header_bytes() {
    std::cout << "\n[CW-U-08] Raw header bytes\n";
    const std::uint8_t win = 12;
    const std::string path = make_valid_archive("cw_header_bytes.cmp2", win);
    const auto bytes = read_bytes(path);

    REQUIRE(bytes.size() >= 6, "CW-U-08a: file has at least 6 header bytes");
    REQUIRE(bytes[0] == 'C', "CW-U-08b: magic[0] == 'C'");
    REQUIRE(bytes[1] == 'M', "CW-U-08c: magic[1] == 'M'");
    REQUIRE(bytes[2] == 'P', "CW-U-08d: magic[2] == 'P'");
    REQUIRE(bytes[3] == '2', "CW-U-08e: magic[3] == '2'");
    REQUIRE(bytes[4] == cmp2::kSupportedVersion, "CW-U-08f: version byte == kSupportedVersion");
    REQUIRE(bytes[5] == win, "CW-U-08g: log2Window byte matches argument");
}

// CW-U-09: Three-block file: only the last block has isLast set.
static void test_three_block_file() {
    std::cout << "\n[CW-U-09] Three-block file ordering\n";
    const std::string path = tp("cw_three_blocks.cmp2");
    cmp2::write_container(path, 15, {
        make_stored(false),
        make_stored(false),
        make_stored(true)
    });
    const auto file = cmp2::read_container(path);
    REQUIRE(file.blocks.size() == 3,              "CW-U-09a: three blocks read back");
    REQUIRE(!file.blocks[0].isLast,               "CW-U-09b: block 0 isLast == false");
    REQUIRE(!file.blocks[1].isLast,               "CW-U-09c: block 1 isLast == false");
    REQUIRE( file.blocks[2].isLast,               "CW-U-09d: block 2 isLast == true");
}

// CW-U-10: CRC field is stored verbatim -- the container does NOT recompute it.
static void test_crc_stored_verbatim() {
    std::cout << "\n[CW-U-10] CRC stored verbatim\n";
    const std::string path = tp("cw_crc_verbatim.cmp2");
    ContainerBlock b = make_stored(true);
    b.crc32 = 0xDEADBEEFU;  // obviously wrong CRC for the payload bytes
    cmp2::write_container(path, 15, {b});

    const auto file = cmp2::read_container(path);
    REQUIRE(file.blocks[0].crc32 == 0xDEADBEEFU,
            "CW-U-10: container preserves caller-supplied CRC verbatim");
}

// CW-U-11: Large sourceLength (requires multi-byte varint) is preserved exactly.
// kMaxSourceBytesPerBlock = 1 MiB = 1048576.  We use a value > 127 (needs 2 varint bytes).
static void test_large_source_length_varint() {
    std::cout << "\n[CW-U-11] Large sourceLength (multi-byte varint)\n";
    const std::string path = tp("cw_large_srclen.cmp2");
    // Build a compressed block with sourceLength = 300 (two varint bytes: 0xAC 0x02)
    ContainerBlock b;
    b.type         = BlockType::Compressed;
    b.isLast       = true;
    b.sourceLength = 300;
    b.payload      = {0x11, 0x22, 0x33, 0x44};
    b.payloadBits  = 32;
    b.crc32        = 0xCAFEBABEU;
    cmp2::write_container(path, 15, {b});

    const auto file = cmp2::read_container(path);
    REQUIRE(file.blocks[0].sourceLength == 300,
            "CW-U-11: large sourceLength survives write->read round-trip");
}

// ===========================================================================
// ContainerReader tests
// ===========================================================================

// CR-U-01: Open a nonexistent file -> FileOpenFailed.
static void test_open_nonexistent_file() {
    std::cout << "\n[CR-U-01] Open nonexistent file\n";
    expect_error(ErrorCode::FileOpenFailed, "CR-U-01", [] {
        cmp2::ContainerReader::open("/no/such/file/ever.cmp2");
    });
}

// CR-U-02: log2Window byte = 16 (> kMaxWindowLog2 = 15) -> InvalidWindow.
static void test_bad_log2window_in_file() {
    std::cout << "\n[CR-U-02] log2Window = 16 in file\n";
    const std::string src  = make_valid_archive("cr_src_for_window.cmp2");
    const std::string path = tp("cr_bad_window.cmp2");
    auto bytes = read_bytes(src);
    bytes[5] = 16;   // log2Window byte
    write_bytes(path, bytes);
    expect_error(ErrorCode::InvalidWindow, "CR-U-02",
                 [&] { cmp2::ContainerReader::open(path); });
}

// CR-U-03: BlockType bits = 2 (invalid) in flags byte -> InvalidBlockType.
// Flags layout: bit0=isLast, bits[2:1]=BlockType, bits[7:3]=reserved(0).
// BlockType=2 -> flags = (2<<1) = 0x04.  Reserved bits stay 0.
static void test_invalid_block_type_in_file() {
    std::cout << "\n[CR-U-03] BlockType = 2 in flags byte\n";
    const std::string src  = make_valid_archive("cr_src_for_type.cmp2");
    const std::string path = tp("cr_bad_block_type.cmp2");
    auto bytes = read_bytes(src);
    // Byte 6 is the first block's flags byte.
    // Clear bits[2:1] then set them to 10 (=2).
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0xF9U) | 0x04U);
    write_bytes(path, bytes);
    expect_error(ErrorCode::InvalidBlockType, "CR-U-03",
                 [&] { cmp2::read_container(path); });
}

// CR-U-04: Reserved flag bits set -> InvalidFlags.
// Bits [7:3] in the flags byte must be zero.
static void test_reserved_flag_bits() {
    std::cout << "\n[CR-U-04] Reserved flag bits set\n";
    const std::string src  = make_valid_archive("cr_src_for_reserved.cmp2");
    const std::string path = tp("cr_reserved_flags.cmp2");
    auto bytes = read_bytes(src);
    bytes[6] = static_cast<std::uint8_t>(bytes[6] | 0x08U);  // set bit 3
    write_bytes(path, bytes);
    expect_error(ErrorCode::InvalidFlags, "CR-U-04",
                 [&] { cmp2::read_container(path); });
}

// CR-U-05: readNextBlock() called again after the final block -> InvalidBlockOrder.
static void test_read_after_final_block() {
    std::cout << "\n[CR-U-05] readNextBlock() after final block\n";
    const std::string path = make_valid_archive("cr_read_after_last.cmp2");
    expect_error(ErrorCode::InvalidBlockOrder, "CR-U-05", [&] {
        cmp2::ContainerReader r = cmp2::ContainerReader::open(path);
        r.readNextBlock();  // reads the only (last) block
        r.readNextBlock();  // must throw InvalidBlockOrder
    });
}

// CR-U-06: finish() with no final block read -> MissingFinalBlock.
// Build a file whose isLast flag is cleared (by patching), then call finish().
static void test_finish_without_final_block() {
    std::cout << "\n[CR-U-06] finish() with no final block\n";
    // We cannot create this via ContainerWriter (it validates), so patch it.
    const std::string src  = make_valid_archive("cr_src_for_no_last.cmp2");
    const std::string path = tp("cr_no_last_block.cmp2");
    auto bytes = read_bytes(src);
    bytes[6] = static_cast<std::uint8_t>(bytes[6] & ~0x01U);  // clear isLast bit
    write_bytes(path, bytes);
    expect_error(ErrorCode::MissingFinalBlock, "CR-U-06", [&] {
        cmp2::ContainerReader r = cmp2::ContainerReader::open(path);
        r.finish();  // no blocks read, and file has no isLast block
    });
}

// CR-U-07: Stored block with sourceLength != payloadBytes in the file -> InvalidBlock.
// Patch the sourceLength varint (byte 7 for a 1-byte varint) to a wrong value.
static void test_stored_source_length_mismatch() {
    std::cout << "\n[CR-U-07] Stored block: sourceLength != payloadBytes\n";
    const std::string src  = make_valid_archive("cr_src_for_srclen.cmp2");
    const std::string path = tp("cr_srclen_mismatch.cmp2");
    auto bytes = read_bytes(src);
    // Byte 7 is sourceLength varint (single byte = 4 for a 4-byte payload).
    // Set it to 5 (!=  payload size 4) to trigger the invariant check.
    bytes[7] = 0x05;
    write_bytes(path, bytes);
    expect_error(ErrorCode::InvalidBlock, "CR-U-07",
                 [&] { cmp2::read_container(path); });
}

// CR-U-08: Non-canonical varint (0x80 0x00 encoding of 0) -> CorruptHeader.
// Inject this sequence as the sourceLength of a block.
// File layout: 6-byte header | flags(1) | sourceLength varint | ...
// We replace byte 7 onwards with 0x80 0x00 (non-canonical 0) + rest.
static void test_non_canonical_varint() {
    std::cout << "\n[CR-U-08] Non-canonical varint (0x80 0x00)\n";
    // Build a minimal header + a block with the varint replaced.
    // Minimal valid header: 'C','M','P','2', 0x01, 0x0F
    // Then: flags=0x01 (Stored, isLast), sourceLength=0x80 0x00 (non-canonical 0)
    // We don't need a full payload because the reader will throw before it reads it.
    std::vector<std::uint8_t> raw = {
        'C','M','P','2', 0x01, 0x0F,   // header
        0x01,                           // flags: Stored, isLast
        0x80, 0x00,                     // sourceLength: non-canonical encoding of 0
        0x00,                           // payloadBytes: 0
        0x00,                           // payloadBits: 0
        0x00, 0x00, 0x00, 0x00         // CRC32
    };
    const std::string path = tp("cr_non_canonical_varint.cmp2");
    write_bytes(path, raw);
    expect_error(ErrorCode::CorruptHeader, "CR-U-08",
                 [&] { cmp2::read_container(path); });
}

// CR-U-09: Unterminated varint (10 continuation bytes) -> CorruptHeader.
// The reader loop runs for indices 0..9; if none has bit-7 clear it falls
// through to fail("unterminated varint"). Use 0x81 for all 10 bytes (data=1,
// avoids the overflow check at index 9 which requires data <= 1).
static void test_unterminated_varint() {
    std::cout << "\n[CR-U-09] Unterminated varint (10 continuation bytes)\n";
    std::vector<std::uint8_t> raw = {
        'C','M','P','2', 0x01, 0x0F,  // header
        0x01,                          // flags
        // 10 bytes, all with bit-7 set -> unterminated
        0x81,0x81,0x81,0x81,0x81,
        0x81,0x81,0x81,0x81,0x81
    };
    const std::string path = tp("cr_unterminated_varint.cmp2");
    write_bytes(path, raw);
    expect_error(ErrorCode::CorruptHeader, "CR-U-09",
                 [&] { cmp2::read_container(path); });
}

// CR-U-10: CRC value 0x00000000 round-trips correctly.
static void test_crc_zero_roundtrip() {
    std::cout << "\n[CR-U-10] CRC == 0x00000000 round-trips\n";
    const std::string path = tp("cr_crc_zero.cmp2");
    ContainerBlock b = make_stored(true);
    b.crc32 = 0x00000000U;
    cmp2::write_container(path, 15, {b});
    const auto file = cmp2::read_container(path);
    REQUIRE(file.blocks[0].crc32 == 0x00000000U,
            "CR-U-10: CRC 0x00000000 survives round-trip");
}

// CR-U-11: CRC value 0xFFFFFFFF round-trips correctly.
static void test_crc_max_roundtrip() {
    std::cout << "\n[CR-U-11] CRC == 0xFFFFFFFF round-trips\n";
    const std::string path = tp("cr_crc_max.cmp2");
    ContainerBlock b = make_stored(true);
    b.crc32 = 0xFFFFFFFFU;
    cmp2::write_container(path, 15, {b});
    const auto file = cmp2::read_container(path);
    REQUIRE(file.blocks[0].crc32 == 0xFFFFFFFFU,
            "CR-U-11: CRC 0xFFFFFFFF survives round-trip");
}

// CR-U-12: hasNextBlock() returns false after the final block is read.
static void test_has_next_block_after_last() {
    std::cout << "\n[CR-U-12] hasNextBlock() == false after final block\n";
    const std::string path = make_valid_archive("cr_has_next.cmp2");
    cmp2::ContainerReader r = cmp2::ContainerReader::open(path);

    REQUIRE(r.hasNextBlock(), "CR-U-12a: hasNextBlock() true before reading");
    const auto block = r.readNextBlock();
    REQUIRE(block.isLast,    "CR-U-12b: block is marked as last");
    REQUIRE(!r.hasNextBlock(),"CR-U-12c: hasNextBlock() false after final block");
    r.finish();  // must not throw
    record_pass("CR-U-12d: finish() after last block does not throw");
}

} // namespace

// ===========================================================================
// main
// ===========================================================================
int main() {
    std::filesystem::remove_all(kTmpDir);
    std::filesystem::create_directories(kTmpDir);

    std::cout << "==========================================================\n";
    std::cout << "  W3 Extended Container Unit Tests (test_container_extended)\n";
    std::cout << "==========================================================\n";

    // ContainerWriter tests
    test_write_after_finish();
    test_write_after_last_block();
    test_null_payload_with_nonzero_size();
    test_source_length_exceeds_limit();
    test_payload_bits_exceeds_bytes();
    test_compressed_zero_source_length();
    test_open_invalid_window();
    test_raw_header_bytes();
    test_three_block_file();
    test_crc_stored_verbatim();
    test_large_source_length_varint();

    // ContainerReader tests
    test_open_nonexistent_file();
    test_bad_log2window_in_file();
    test_invalid_block_type_in_file();
    test_reserved_flag_bits();
    test_read_after_final_block();
    test_finish_without_final_block();
    test_stored_source_length_mismatch();
    test_non_canonical_varint();
    test_unterminated_varint();
    test_crc_zero_roundtrip();
    test_crc_max_roundtrip();
    test_has_next_block_after_last();

    std::filesystem::remove_all(kTmpDir);

    std::cout << "\n----------------------------------------------------------\n";
    std::cout << "  Passed: " << g_passed << "  |  Failed: " << g_failed << "\n";
    std::cout << "----------------------------------------------------------\n";

    if (g_failed == 0) {
        std::cout << "  All Container extended tests PASSED.\n";
    } else {
        std::cerr << "  " << g_failed << " test(s) FAILED.\n";
    }
    return g_failed == 0 ? 0 : 1;
}
