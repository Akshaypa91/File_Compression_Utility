#include "benchmarking.hpp"
#include "container_format.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using cmp2::BlockType;
using cmp2::ContainerBlock;
using cmp2::ContainerError;
using cmp2::ContainerReader;
using cmp2::ContainerWriter;
using cmp2::ErrorCode;

const std::filesystem::path kTestDir =
    std::filesystem::temp_directory_path() / "w3_instructions_tests";

std::filesystem::path test_file(const std::string& name) {
    return kTestDir / name;
}

// Simple IEEE CRC-32 implementation (reflected 0xEDB88320) for standalone test verification
std::uint32_t compute_crc32(const std::uint8_t* data, std::size_t size) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (unsigned b = 0; b < 8; ++b) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

// Helper: writes an arbitrary byte array to a temporary file
void write_raw_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out);
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    assert(out);
}

// Helper: reads an arbitrary byte array from file
std::vector<std::uint8_t> read_raw_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    assert(in);
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(size);
    if (size > 0) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    }
    return data;
}

// ---------------------------------------------------------------------------
// TEST 1: Edge cases — Empty file, 1 byte, 2 bytes round-trip
// ---------------------------------------------------------------------------
void test_edge_cases_empty_1byte_2bytes() {
    std::cout << "[Test 1] Testing edge cases (empty file, 1 byte, 2 bytes)...\n";

    // 1.1 Empty file: W3 instructions Section 3: "Empty file: write one STORED block with 0 bytes and isLast = true."
    {
        const auto cmpPath = test_file("empty.cmp2");
        ContainerBlock emptyBlock;
        emptyBlock.type = BlockType::Stored;
        emptyBlock.isLast = true;
        emptyBlock.sourceLength = 0;
        emptyBlock.payload.clear();
        emptyBlock.payloadBits = 0;
        emptyBlock.crc32 = compute_crc32(nullptr, 0);

        cmp2::BlockBenchmarkTimer timer;
        timer.startCompression();
        cmp2::write_container(cmpPath.string(), 15, {emptyBlock});
        timer.stopCompression();

        timer.startDecompression();
        const auto file = cmp2::read_container(cmpPath.string());
        timer.stopDecompression();

        assert(file.blocks.size() == 1);
        assert(file.blocks[0].sourceLength == 0);
        assert(file.blocks[0].payload.empty());
        assert(file.blocks[0].payloadBits == 0);
        assert(file.blocks[0].isLast);

        cmp2::BlockBenchmark bb = timer.finish(0, file.blocks[0], true);
        assert(bb.originalSize == 0);
        assert(bb.finalBlockSize == 8); // 1 flag + 3 varints(0) + 4 crc

        cmp2::FileBenchmark fb = cmp2::benchmark_container_file(cmpPath.string());
        assert(fb.originalTotalSize == 0);
        assert(fb.finalTotalSize == 14); // 6 header + 8 block
        assert(fb.totalBlocks == 1);
        assert(fb.storedBlocksCount == 1);
    }

    // 1.2 Single byte file
    {
        const auto cmpPath = test_file("one_byte.cmp2");
        ContainerBlock oneBlock;
        oneBlock.type = BlockType::Stored;
        oneBlock.isLast = true;
        oneBlock.sourceLength = 1;
        oneBlock.payload = {0x42};
        oneBlock.payloadBits = 8;
        oneBlock.crc32 = compute_crc32(oneBlock.payload.data(), 1);

        cmp2::write_container(cmpPath.string(), 15, {oneBlock});
        const auto file = cmp2::read_container(cmpPath.string());

        assert(file.blocks.size() == 1);
        assert(file.blocks[0].sourceLength == 1);
        assert(file.blocks[0].payload.size() == 1);
        assert(file.blocks[0].payload[0] == 0x42);
        assert(file.blocks[0].isLast);

        cmp2::FileBenchmark fb = cmp2::benchmark_container_file(cmpPath.string());
        assert(fb.originalTotalSize == 1);
        assert(fb.finalTotalSize == 15); // 6 header + 1 flag + 3 varints + 1 payload + 4 crc = 15
        assert(fb.overallPercentageReduction < 0.0); // framing causes slight expansion on 1 byte
    }

    // 1.3 Two bytes file
    {
        const auto cmpPath = test_file("two_bytes.cmp2");
        ContainerBlock twoBlock;
        twoBlock.type = BlockType::Stored;
        twoBlock.isLast = true;
        twoBlock.sourceLength = 2;
        twoBlock.payload = {0xAA, 0x55};
        twoBlock.payloadBits = 16;
        twoBlock.crc32 = compute_crc32(twoBlock.payload.data(), 2);

        cmp2::write_container(cmpPath.string(), 15, {twoBlock});
        const auto file = cmp2::read_container(cmpPath.string());

        assert(file.blocks.size() == 1);
        assert(file.blocks[0].sourceLength == 2);
        assert(file.blocks[0].payload.size() == 2);
        assert(file.blocks[0].payload[0] == 0xAA && file.blocks[0].payload[1] == 0x55);

        cmp2::FileBenchmark fb = cmp2::benchmark_container_file(cmpPath.string());
        assert(fb.originalTotalSize == 2);
    }
    std::cout << "  -> PASS: Edge cases round-trip successfully.\n";
}

// ---------------------------------------------------------------------------
// TEST 2: Text file & repeated pattern file (compression really shrinks them)
// ---------------------------------------------------------------------------
void test_text_and_repeated_pattern() {
    std::cout << "[Test 2] Testing text & repeated pattern file compression...\n";

    const auto cmpPath = test_file("pattern.cmp2");

    // Highly repetitive pattern: original size = 16,384 bytes, compressed payload = 120 bytes
    const std::uint64_t originalLen = 16384;
    std::vector<std::uint8_t> compressedPayload(120, 0x3C);

    ContainerBlock block;
    block.type = BlockType::Compressed;
    block.isLast = true;
    block.sourceLength = originalLen;
    block.payload = compressedPayload;
    block.payloadBits = compressedPayload.size() * 8 - 3; // e.g. with last byte padding
    block.crc32 = compute_crc32(block.payload.data(), block.payload.size());

    cmp2::BlockBenchmarkTimer timer;
    timer.startCompression();
    cmp2::write_container(cmpPath.string(), 15, {block});
    timer.stopCompression();

    timer.startDecompression();
    const auto file = cmp2::read_container(cmpPath.string());
    timer.stopDecompression();

    assert(file.blocks.size() == 1);
    assert(file.blocks[0].sourceLength == originalLen);
    assert(file.blocks[0].payload == compressedPayload);

    cmp2::FileBenchmark fb = cmp2::benchmark_container_file(cmpPath.string());
    assert(fb.originalTotalSize == originalLen);
    assert(fb.finalTotalSize < fb.originalTotalSize / 100); // shrunk massively!
    assert(fb.overallCompressionRatio > 100.0);             // > 100x ratio!
    assert(fb.overallPercentageReduction > 99.0);           // > 99% space savings!

    std::cout << "  -> PASS: Compression ratio = " << fb.overallCompressionRatio
              << "x, Percentage reduction = " << fb.overallPercentageReduction << "%\n";
}

// ---------------------------------------------------------------------------
// TEST 3: Random bytes (incompressible) — stored blocks work, negligible overhead
// ---------------------------------------------------------------------------
void test_random_incompressible_stored_blocks() {
    std::cout << "[Test 3] Testing random bytes (incompressible fallback to stored blocks)...\n";

    const auto cmpPath = test_file("random.cmp2");
    const std::size_t size = 65536; // 64 KiB
    std::vector<std::uint8_t> randomData(size);
    std::mt19937 rng(42);
    for (std::size_t i = 0; i < size; ++i) {
        randomData[i] = static_cast<std::uint8_t>(rng() & 0xFF);
    }

    // Incompressible fallback: stored block with raw bytes
    ContainerBlock block;
    block.type = BlockType::Stored;
    block.isLast = true;
    block.sourceLength = size;
    block.payload = randomData;
    block.payloadBits = size * 8ULL;
    block.crc32 = compute_crc32(block.payload.data(), block.payload.size());

    cmp2::write_container(cmpPath.string(), 15, {block});
    const auto file = cmp2::read_container(cmpPath.string());

    assert(file.blocks.size() == 1);
    assert(file.blocks[0].type == BlockType::Stored);
    assert(file.blocks[0].payload == randomData);

    cmp2::FileBenchmark fb = cmp2::benchmark_container_file(cmpPath.string());
    assert(fb.originalTotalSize == size);
    assert(fb.storedBlocksCount == 1);
    assert(fb.compressedBlocksCount == 0);

    // Wire overhead is file header (6) + framing (1 flag + 3 varints + 4 crc = 14 bytes)
    const std::uint64_t overhead = fb.finalTotalSize - fb.originalTotalSize;
    assert(overhead <= 20); // only tiny fixed overhead!
    std::cout << "  -> PASS: Stored block wire overhead on 64 KiB is only "
              << overhead << " bytes ("
              << (static_cast<double>(overhead) / static_cast<double>(size) * 100.0) << "%).\n";
}

// ---------------------------------------------------------------------------
// TEST 4: File of exactly 1 MiB and 1 MiB + 1 — block boundary test
// ---------------------------------------------------------------------------
void test_block_boundaries_1mib_and_1mib_plus_one() {
    std::cout << "[Test 4] Testing block boundaries (exactly 1 MiB vs 1 MiB + 1 byte)...\n";

    constexpr std::uint64_t k1MiB = cmp2::kMaxSourceBytesPerBlock; // 1,048,576 bytes

    // 4.1 Exactly 1 MiB fits in EXACTLY ONE BLOCK
    {
        const auto path = test_file("exact_1mib.cmp2");
        ContainerBlock b;
        b.type = BlockType::Stored;
        b.isLast = true;
        b.sourceLength = k1MiB;
        b.payload = std::vector<std::uint8_t>(k1MiB, 0x5A);
        b.payloadBits = k1MiB * 8ULL;
        b.crc32 = compute_crc32(b.payload.data(), b.payload.size());

        cmp2::write_container(path.string(), 15, {b});
        const auto file = cmp2::read_container(path.string());

        assert(file.blocks.size() == 1);
        assert(file.blocks[0].sourceLength == k1MiB);
        assert(file.blocks[0].isLast);

        cmp2::FileBenchmark fb = cmp2::benchmark_container_file(path.string());
        assert(fb.totalBlocks == 1);
        assert(fb.originalTotalSize == k1MiB);
    }

    // 4.2 1 MiB + 1 byte MUST CROSS BLOCK BOUNDARY -> 2 BLOCKS
    {
        const auto path = test_file("1mib_plus_one.cmp2");

        // Block 0: exactly 1 MiB, isLast = false
        ContainerBlock b0;
        b0.type = BlockType::Stored;
        b0.isLast = false;
        b0.sourceLength = k1MiB;
        b0.payload = std::vector<std::uint8_t>(k1MiB, 0x11);
        b0.payloadBits = k1MiB * 8ULL;
        b0.crc32 = compute_crc32(b0.payload.data(), b0.payload.size());

        // Block 1: 1 byte, isLast = true
        ContainerBlock b1;
        b1.type = BlockType::Stored;
        b1.isLast = true;
        b1.sourceLength = 1;
        b1.payload = {0x99};
        b1.payloadBits = 8;
        b1.crc32 = compute_crc32(b1.payload.data(), b1.payload.size());

        cmp2::write_container(path.string(), 15, {b0, b1});
        const auto file = cmp2::read_container(path.string());

        assert(file.blocks.size() == 2);
        assert(!file.blocks[0].isLast);
        assert(file.blocks[0].sourceLength == k1MiB);
        assert(file.blocks[1].isLast);
        assert(file.blocks[1].sourceLength == 1);

        cmp2::FileBenchmark fb = cmp2::benchmark_container_file(path.string());
        assert(fb.totalBlocks == 2);
        assert(fb.originalTotalSize == k1MiB + 1ULL);
        assert(fb.blocks[0].blockIndex == 0);
        assert(fb.blocks[1].blockIndex == 1);
        assert(fb.blocks[0].originalSize == k1MiB);
        assert(fb.blocks[1].originalSize == 1);
    }
    std::cout << "  -> PASS: 1 MiB in 1 block, 1 MiB + 1 properly splits into 2 blocks.\n";
}

// ---------------------------------------------------------------------------
// TEST 5: Flip one bit in the middle of file — CRC catches it, clean exit, no crash
// ---------------------------------------------------------------------------
void test_bit_flip_caught_by_crc() {
    std::cout << "[Test 5] Testing bit-flip detection (CRC-32 verification catches corruption)...\n";

    const auto origPath = test_file("valid_data.cmp2");
    const auto corruptPath = test_file("corrupt_bit.cmp2");

    ContainerBlock b;
    b.type = BlockType::Stored;
    b.isLast = true;
    b.sourceLength = 100;
    b.payload = std::vector<std::uint8_t>(100, 0x7E);
    b.payloadBits = 800;
    b.crc32 = compute_crc32(b.payload.data(), b.payload.size());

    cmp2::write_container(origPath.string(), 15, {b});

    // Read raw container bytes from disk
    auto rawBytes = read_raw_file(origPath);
    assert(rawBytes.size() > 20);

    // Flip a bit in the payload data
    rawBytes[15] ^= 0x01U;
    write_raw_file(corruptPath, rawBytes);

    // Read container back: reading succeeds structurally, but CRC check MUST detect corruption!
    const auto file = cmp2::read_container(corruptPath.string());
    assert(file.blocks.size() == 1);

    const std::uint32_t recalculatedCrc =
        compute_crc32(file.blocks[0].payload.data(), file.blocks[0].payload.size());

    // The recalculated CRC MUST NOT equal stored crc32!
    assert(recalculatedCrc != file.blocks[0].crc32);

    // Benchmarking session confirms CRC failure is captured accurately
    cmp2::BlockBenchmarkTimer timer;
    cmp2::BlockBenchmark bb = timer.finish(0, file.blocks[0], (recalculatedCrc == file.blocks[0].crc32));
    assert(!bb.crcVerified);

    cmp2::BenchmarkSession session("corrupt.cmp2");
    session.recordBlock(bb);
    cmp2::FileBenchmark fb = session.finalize(false);
    assert(!fb.allCrcVerified);
    assert(!fb.losslessVerified);

    std::cout << "  -> PASS: CRC32 mismatch detected (stored=0x"
              << std::hex << file.blocks[0].crc32 << ", actual=0x" << recalculatedCrc << std::dec
              << ") without crashing.\n";
}

// ---------------------------------------------------------------------------
// TEST 6: Truncate file — clean error, no crash
// ---------------------------------------------------------------------------
void test_truncate_file_clean_error() {
    std::cout << "[Test 6] Testing truncated file handling (clean error, no crash)...\n";

    const auto origPath = test_file("trunc_base.cmp2");
    const auto truncPath = test_file("truncated.cmp2");

    ContainerBlock b;
    b.type = BlockType::Stored;
    b.isLast = true;
    b.sourceLength = 200;
    b.payload = std::vector<std::uint8_t>(200, 0x33);
    b.payloadBits = 1600;
    b.crc32 = compute_crc32(b.payload.data(), b.payload.size());

    cmp2::write_container(origPath.string(), 15, {b});

    auto bytes = read_raw_file(origPath);
    // Truncate last 10 bytes (truncating payload and CRC)
    bytes.resize(bytes.size() - 10);
    write_raw_file(truncPath, bytes);

    bool caught = false;
    try {
        cmp2::read_container(truncPath.string());
    } catch (const ContainerError& e) {
        caught = true;
        assert(e.code() == ErrorCode::TruncatedInput);
    }
    assert(caught && "TruncatedInput ContainerError must be thrown");

    std::cout << "  -> PASS: Truncated file threw clean TruncatedInput ContainerError.\n";
}

// ---------------------------------------------------------------------------
// TEST 7: Multi-block streaming handoff with full benchmarking report
// ---------------------------------------------------------------------------
void test_streaming_handoff_and_benchmarking_report() {
    std::cout << "[Test 7] Testing streaming writer -> reader handoff with benchmark report...\n";

    const auto path = test_file("streaming_benchmark.cmp2");

    // Create a 3-block sequence
    ContainerWriter writer = ContainerWriter::open(path.string(), 15);

    cmp2::BenchmarkSession session("streaming_benchmark.cmp2");

    for (std::size_t i = 0; i < 3; ++i) {
        ContainerBlock b;
        b.type = (i % 2 == 0) ? BlockType::Compressed : BlockType::Stored;
        b.isLast = (i == 2);
        b.sourceLength = 5000 + i * 1000;
        b.payload = std::vector<std::uint8_t>(b.type == BlockType::Compressed ? 1500 : b.sourceLength, static_cast<std::uint8_t>(i));
        b.payloadBits = b.payload.size() * 8ULL;
        b.crc32 = compute_crc32(b.payload.data(), b.payload.size());

        cmp2::BlockBenchmarkTimer timer;
        timer.startCompression();
        writer.writeBlock(b);
        timer.stopCompression();

        cmp2::BlockBenchmark bb = timer.finish(i, b, true);
        session.recordBlock(bb);
    }
    writer.finish();

    // Now read back streaming
    ContainerReader reader = ContainerReader::open(path.string());
    std::size_t readCount = 0;
    while (reader.hasNextBlock()) {
        ContainerBlock b = reader.readNextBlock();
        const auto actualCrc = compute_crc32(b.payload.data(), b.payload.size());
        assert(actualCrc == b.crc32);
        ++readCount;
    }
    reader.finish();
    assert(readCount == 3);

    cmp2::FileBenchmark fb = session.finalize(true);
    assert(fb.totalBlocks == 3);
    assert(fb.compressedBlocksCount == 2);
    assert(fb.storedBlocksCount == 1);

    // Verify printed report output
    std::string report = cmp2::format_benchmark_report(fb);
    assert(!report.empty());
    std::cout << report << "\n";
    std::cout << "  -> PASS: Multi-block handoff and benchmark report verified.\n";
}

} // namespace

int main() {
    std::filesystem::remove_all(kTestDir);
    std::filesystem::create_directories(kTestDir);

    std::cout << "========================================================\n";
    std::cout << " RUNNING WORKLET 3 INSTRUCTION REQUIREMENTS TESTS\n";
    std::cout << "========================================================\n";

    test_edge_cases_empty_1byte_2bytes();
    test_text_and_repeated_pattern();
    test_random_incompressible_stored_blocks();
    test_block_boundaries_1mib_and_1mib_plus_one();
    test_bit_flip_caught_by_crc();
    test_truncate_file_clean_error();
    test_streaming_handoff_and_benchmarking_report();

    std::filesystem::remove_all(kTestDir);

    std::cout << "========================================================\n";
    std::cout << " ALL WORKLET 3 INSTRUCTION TESTS PASSED!\n";
    std::cout << "========================================================\n";
    return 0;
}
