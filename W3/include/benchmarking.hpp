#pragma once

#include "container_format.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace cmp2 {

// ---------------------------------------------------------------------------
// Helper functions for framing and metric calculations
// ---------------------------------------------------------------------------

// Computes the number of bytes required to encode `value` as an unsigned LEB128 varint.
constexpr std::size_t varint_length(std::uint64_t value) noexcept {
    std::size_t length = 0;
    do {
        ++length;
        value >>= 7U;
    } while (value != 0);
    return length;
}

// Fixed size of the CMP2 file header (Magic [4] + Version [1] + log2Window [1])
constexpr std::size_t kContainerHeaderBytes = 6;

// Computes the wire framing overhead for a single block:
// 1 byte flags + varint(sourceLength) + varint(payloadBytes) + varint(payloadBits) + 4 bytes CRC32
std::size_t calculate_framing_overhead(const ContainerBlock& block) noexcept;

// Computes the total wire size of a block: framing overhead + payload bytes.
std::size_t calculate_total_block_size(const ContainerBlock& block) noexcept;

// Computes compression ratio: originalSize / finalSize (e.g. 2.50 for 2.5x).
// Returns 1.0 if originalSize == 0 and finalSize == 0.
// Returns 0.0 if finalSize == 0.
double calculate_compression_ratio(std::uint64_t originalSize,
                                   std::uint64_t finalSize) noexcept;

// Computes space ratio (inverse of compression ratio): finalSize / originalSize.
// Returns 1.0 if originalSize == 0.
double calculate_space_ratio(std::uint64_t originalSize,
                             std::uint64_t finalSize) noexcept;

// Computes percentage reduction / space saving:
// ((originalSize - finalSize) / originalSize) * 100.0%.
// Can be negative if the compressed block expanded (e.g. framing overhead on tiny input).
// Returns 0.0 if originalSize == 0.
double calculate_percentage_reduction(std::uint64_t originalSize,
                                      std::uint64_t finalSize) noexcept;

// Computes throughput in Megabytes per second (MB/s = 10^6 bytes/sec).
double calculate_throughput_mbs(std::uint64_t bytes,
                                std::uint64_t durationNs) noexcept;

// ---------------------------------------------------------------------------
// Benchmark metrics for a single container block
// ---------------------------------------------------------------------------
struct BlockBenchmark {
    std::size_t blockIndex = 0;
    BlockType type = BlockType::Stored;
    bool isLast = false;

    // Size metrics (in bytes)
    std::uint64_t originalSize = 0;    // sourceLength (uncompressed bytes covered by this block)
    std::uint64_t payloadBytes = 0;    // stored payload byte count
    std::uint64_t payloadBits = 0;     // meaningful bits inside the payload
    std::uint64_t framingOverhead = 0; // flags (1) + 3 varints + CRC32 (4)
    std::uint64_t finalBlockSize = 0;  // framingOverhead + payloadBytes

    // Timing metrics (nanoseconds)
    std::uint64_t compressionTimeNs = 0;
    std::uint64_t decompressionTimeNs = 0;

    // Derived speed metrics (MB/s)
    double compressionSpeedMBs = 0.0;
    double decompressionSpeedMBs = 0.0;

    // Compression effectiveness metrics
    double compressionRatio = 1.0;     // originalSize / finalBlockSize
    double inverseRatio = 1.0;        // finalBlockSize / originalSize
    double percentageReduction = 0.0; // ((originalSize - finalBlockSize) / originalSize) * 100%

    // Integrity
    std::uint32_t crc32 = 0;
    bool crcVerified = false;

    // Convenience timing helpers
    double compressionTimeMs() const noexcept {
        return static_cast<double>(compressionTimeNs) / 1e6;
    }
    double decompressionTimeMs() const noexcept {
        return static_cast<double>(decompressionTimeNs) / 1e6;
    }
};

// ---------------------------------------------------------------------------
// Combined benchmark metrics for the whole file
// ---------------------------------------------------------------------------
struct FileBenchmark {
    std::string filename;

    // Overall sizes (in bytes)
    std::uint64_t originalTotalSize = 0;   // Sum of sourceLength across all blocks
    std::uint64_t containerHeaderSize = kContainerHeaderBytes; // 6 bytes fixed CMP2 header
    std::uint64_t totalPayloadBytes = 0;   // Sum of payload bytes across all blocks
    std::uint64_t totalFramingOverhead = 0; // containerHeaderSize + sum of block framing
    std::uint64_t finalTotalSize = 0;      // Total file size on disk

    // Block breakdown
    std::size_t totalBlocks = 0;
    std::size_t storedBlocksCount = 0;
    std::size_t compressedBlocksCount = 0;

    // Overall timing (nanoseconds)
    std::uint64_t totalCompressionTimeNs = 0;
    std::uint64_t totalDecompressionTimeNs = 0;

    // Overall throughput (MB/s)
    double overallCompressionThroughputMBs = 0.0;
    double overallDecompressionThroughputMBs = 0.0;

    // Overall effectiveness
    double overallCompressionRatio = 1.0;
    double overallInverseRatio = 1.0;
    double overallPercentageReduction = 0.0;

    // Verification flags
    bool losslessVerified = false;
    bool allCrcVerified = false;

    // Per-block benchmark breakdown
    std::vector<BlockBenchmark> blocks;

    // Convenience timing helpers
    double totalCompressionTimeMs() const noexcept {
        return static_cast<double>(totalCompressionTimeNs) / 1e6;
    }
    double totalDecompressionTimeMs() const noexcept {
        return static_cast<double>(totalDecompressionTimeNs) / 1e6;
    }
    double totalCompressionTimeSec() const noexcept {
        return static_cast<double>(totalCompressionTimeNs) / 1e9;
    }
    double totalDecompressionTimeSec() const noexcept {
        return static_cast<double>(totalDecompressionTimeNs) / 1e9;
    }
};

// ---------------------------------------------------------------------------
// High-resolution block timer helper
// ---------------------------------------------------------------------------
class BlockBenchmarkTimer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    void startCompression() noexcept;
    void stopCompression() noexcept;

    void startDecompression() noexcept;
    void stopDecompression() noexcept;

    std::uint64_t compressionElapsedNs() const noexcept { return compressionNs_; }
    std::uint64_t decompressionElapsedNs() const noexcept { return decompressionNs_; }

    // Generates a populated BlockBenchmark from the timed run and the resulting block.
    BlockBenchmark finish(std::size_t blockIndex,
                          const ContainerBlock& block,
                          bool crcPassed = true) const;

private:
    TimePoint compStart_{};
    TimePoint compEnd_{};
    TimePoint decompStart_{};
    TimePoint decompEnd_{};
    std::uint64_t compressionNs_ = 0;
    std::uint64_t decompressionNs_ = 0;
};

// ---------------------------------------------------------------------------
// Benchmark session builder for aggregating block-by-block results
// ---------------------------------------------------------------------------
class BenchmarkSession {
public:
    explicit BenchmarkSession(std::string filename = "file.cmp2");

    // Records a completed block benchmark into the session.
    void recordBlock(const BlockBenchmark& blockMetric);

    // Finalizes the session and computes combined whole-file metrics.
    FileBenchmark finalize(bool lossless = true);

    // Resets session state for a new file.
    void reset();

    const std::vector<BlockBenchmark>& blocks() const noexcept { return blocks_; }

private:
    std::string filename_;
    std::vector<BlockBenchmark> blocks_;
};

// ---------------------------------------------------------------------------
// Benchmarking runners and formatters
// ---------------------------------------------------------------------------

// Reads an existing CMP2 container file from disk, inspects all blocks,
// measures reading and verification, and returns the complete FileBenchmark.
FileBenchmark benchmark_container_file(const std::string& containerPath);

// Formats a human-readable text report containing file summary and per-block table.
std::string format_benchmark_report(const FileBenchmark& benchmark);

// Formats the benchmark report as CSV (compatible with spreadsheet analysis).
std::string format_benchmark_csv(const FileBenchmark& benchmark);

// Formats the benchmark report as JSON.
std::string format_benchmark_json(const FileBenchmark& benchmark);

// Prints report to an output stream (e.g. std::cout).
void print_benchmark_report(std::ostream& out, const FileBenchmark& benchmark);

} // namespace cmp2
