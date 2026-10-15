#include "benchmarking.hpp"
#include "container_format.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

const std::filesystem::path kTestDir =
    std::filesystem::temp_directory_path() / "cmp2_benchmarking_tests";

std::filesystem::path test_path(const std::string& name) {
    return kTestDir / name;
}

void test_varint_and_framing() {
    assert(cmp2::varint_length(0) == 1);
    assert(cmp2::varint_length(1) == 1);
    assert(cmp2::varint_length(127) == 1);
    assert(cmp2::varint_length(128) == 2);
    assert(cmp2::varint_length(16383) == 2);
    assert(cmp2::varint_length(16384) == 3);

    cmp2::ContainerBlock block;
    block.type = cmp2::BlockType::Stored;
    block.isLast = false;
    block.sourceLength = 100;
    block.payload = std::vector<std::uint8_t>(100, 'A');
    block.payloadBits = 800; // 100 * 8
    block.crc32 = 0x12345678U;

    // Framing: 1 flag + varint(100)=1 + varint(100)=1 + varint(800)=2 + 4 CRC = 9 bytes
    const std::size_t framing = cmp2::calculate_framing_overhead(block);
    assert(framing == 9);

    const std::size_t total = cmp2::calculate_total_block_size(block);
    assert(total == 109);
}

void test_ratio_and_reduction_calculations() {
    // 1000 original -> 500 final: ratio 2.0x, reduction 50.0%
    const double ratio1 = cmp2::calculate_compression_ratio(1000, 500);
    assert(std::fabs(ratio1 - 2.0) < 1e-6);

    const double red1 = cmp2::calculate_percentage_reduction(1000, 500);
    assert(std::fabs(red1 - 50.0) < 1e-6);

    const double inv1 = cmp2::calculate_space_ratio(1000, 500);
    assert(std::fabs(inv1 - 0.5) < 1e-6);

    // Expansion: 10 original -> 18 final: ratio < 1.0, negative reduction
    const double ratio2 = cmp2::calculate_compression_ratio(10, 18);
    assert(ratio2 < 1.0);
    const double red2 = cmp2::calculate_percentage_reduction(10, 18);
    assert(red2 < 0.0);

    // Zero edge cases
    assert(cmp2::calculate_compression_ratio(0, 0) == 1.0);
    assert(cmp2::calculate_percentage_reduction(0, 10) == 0.0);
    assert(cmp2::calculate_compression_ratio(100, 0) == 0.0);
}

void test_timer_and_session() {
    cmp2::BlockBenchmarkTimer timer;

    timer.startCompression();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    timer.stopCompression();

    timer.startDecompression();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    timer.stopDecompression();

    assert(timer.compressionElapsedNs() > 0);
    assert(timer.decompressionElapsedNs() > 0);

    cmp2::ContainerBlock block;
    block.type = cmp2::BlockType::Compressed;
    block.isLast = true;
    block.sourceLength = 1000;
    block.payload = std::vector<std::uint8_t>(400, 0x55);
    block.payloadBits = 3200;
    block.crc32 = 0xAABBCCDD;

    cmp2::BlockBenchmark bb = timer.finish(0, block, true);
    assert(bb.blockIndex == 0);
    assert(bb.type == cmp2::BlockType::Compressed);
    assert(bb.originalSize == 1000);
    assert(bb.payloadBytes == 400);
    assert(bb.crc32 == 0xAABBCCDD);
    assert(bb.crcVerified);
    assert(bb.compressionRatio > 2.0); // 1000 / ~409
    assert(bb.percentageReduction > 50.0);

    cmp2::BenchmarkSession session("sample_test.cmp2");
    session.recordBlock(bb);

    cmp2::FileBenchmark fileReport = session.finalize(true);
    assert(fileReport.filename == "sample_test.cmp2");
    assert(fileReport.totalBlocks == 1);
    assert(fileReport.compressedBlocksCount == 1);
    assert(fileReport.storedBlocksCount == 0);
    assert(fileReport.originalTotalSize == 1000);
    assert(fileReport.totalPayloadBytes == 400);
    assert(fileReport.losslessVerified);
    assert(fileReport.allCrcVerified);
    assert(fileReport.overallCompressionRatio > 2.0);
    assert(fileReport.finalTotalSize == cmp2::kContainerHeaderBytes + bb.finalBlockSize);
}

void test_benchmark_container_file_round_trip() {
    const auto path = test_path("round_trip_bench.cmp2");

    // Block 0: Stored block
    cmp2::ContainerBlock b0;
    b0.type = cmp2::BlockType::Stored;
    b0.isLast = false;
    b0.payload = {0x11, 0x22, 0x33, 0x44};
    b0.sourceLength = 4;
    b0.payloadBits = 32;
    b0.crc32 = 0x11223344;

    // Block 1: Compressed block (final)
    cmp2::ContainerBlock b1;
    b1.type = cmp2::BlockType::Compressed;
    b1.isLast = true;
    b1.sourceLength = 50;
    b1.payload = {0xAA, 0xBB, 0xCC};
    b1.payloadBits = 21;
    b1.crc32 = 0xAABBCCDD;

    cmp2::write_container(path.string(), 15, {b0, b1});

    // Run container file benchmark
    cmp2::FileBenchmark b = cmp2::benchmark_container_file(path.string());

    assert(b.totalBlocks == 2);
    assert(b.storedBlocksCount == 1);
    assert(b.compressedBlocksCount == 1);
    assert(b.originalTotalSize == 54); // 4 + 50
    assert(b.totalPayloadBytes == 7);   // 4 + 3

    // Check exact disk file size against calculated final size
    const auto actualDiskSize = std::filesystem::file_size(path);
    assert(b.finalTotalSize == actualDiskSize);

    // Test text, CSV, and JSON formatters
    const std::string textRep = cmp2::format_benchmark_report(b);
    assert(textRep.find("WORKLET 3 BENCHMARK REPORT") != std::string::npos);
    assert(textRep.find("Stored") != std::string::npos);
    assert(textRep.find("Compressed") != std::string::npos);

    const std::string csvRep = cmp2::format_benchmark_csv(b);
    assert(csvRep.find("block_index,type,is_last") != std::string::npos);
    assert(csvRep.find("summary_total_blocks") != std::string::npos);

    const std::string jsonRep = cmp2::format_benchmark_json(b);
    assert(jsonRep.find("\"originalTotalSize\": 54") != std::string::npos);
    assert(jsonRep.find("\"totalBlocks\": 2") != std::string::npos);
}

} // namespace

int main() {
    std::filesystem::remove_all(kTestDir);
    std::filesystem::create_directories(kTestDir);

    test_varint_and_framing();
    test_ratio_and_reduction_calculations();
    test_timer_and_session();
    test_benchmark_container_file_round_trip();

    std::filesystem::remove_all(kTestDir);
    std::cout << "All Worklet 3 benchmarking unit tests passed!\n";
    return 0;
}
