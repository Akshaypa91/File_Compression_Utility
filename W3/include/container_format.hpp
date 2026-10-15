#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cmp2 {

// CMP2 is deliberately a different wire format from the earlier single-payload
// CMP1 proposal.  Do not silently accept or produce CMP1 with this API.
constexpr char kMagic[4] = {'C', 'M', 'P', '2'};
constexpr std::uint8_t kSupportedVersion = 0x01;
constexpr std::uint8_t kMaxWindowLog2 = 15; // 2^15 = 32 KiB

// The block-size limit is part of the block-wise design.  It prevents a
// malformed file from requesting an unbounded allocation in the reader.
constexpr std::uint64_t kMaxSourceBytesPerBlock = 1ULL << 20; // 1 MiB
constexpr std::uint64_t kMaxPayloadBytesPerBlock = 16ULL << 20; // 16 MiB

enum class BlockType : std::uint8_t {
    Stored = 0,
    Compressed = 1,
};

enum class ErrorCode {
    FileOpenFailed,
    FileReadFailed,
    FileWriteFailed,
    InvalidArgument,
    InvalidSignature,
    UnsupportedVersion,
    InvalidWindow,
    TruncatedInput,
    CorruptHeader,
    InvalidFlags,
    InvalidBlockType,
    InvalidBitLength,
    InvalidBlock,
    InvalidBlockOrder,
    MissingFinalBlock,
    TrailingData,
};

const char* error_code_name(ErrorCode code) noexcept;

class ContainerError final : public std::runtime_error {
public:
    ContainerError(ErrorCode code, const std::string& message);

    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

struct ContainerHeader {
    std::uint8_t version = kSupportedVersion;
    std::uint8_t log2Window = 15;
};

// This is the public handoff object between the pipeline and the container.
// crc32 is supplied by the CRC32 owner; this module only serializes it and
// returns it when reading.
struct ContainerBlock {
    BlockType type = BlockType::Stored;
    bool isLast = false;
    std::uint64_t sourceLength = 0;
    std::uint64_t payloadBits = 0;
    std::vector<std::uint8_t> payload;
    std::uint32_t crc32 = 0;
};

class ContainerWriter final {
public:
    static ContainerWriter open(const std::string& path,
                                std::uint8_t log2Window);

    ContainerWriter(const ContainerWriter&) = delete;
    ContainerWriter& operator=(const ContainerWriter&) = delete;
    ContainerWriter(ContainerWriter&&) noexcept;
    ContainerWriter& operator=(ContainerWriter&&) noexcept;
    ~ContainerWriter() noexcept;

    const ContainerHeader& header() const noexcept { return header_; }

    // Writes exactly one framed block.  The payload must already be complete,
    // so payloadBytes and payloadBits can be serialized before the payload.
    void writeBlock(const ContainerBlock& block);

    // A valid CMP2 file must contain one block marked isLast=true.
    void finish();

private:
    ContainerWriter(std::string path,
                    std::uint8_t log2Window,
                    std::ofstream&& output);

    std::string path_;
    std::ofstream output_;
    ContainerHeader header_{};
    bool lastBlockWritten_ = false;
    bool finished_ = false;
};

class ContainerReader final {
public:
    static ContainerReader open(const std::string& path);

    ContainerReader(const ContainerReader&) = delete;
    ContainerReader& operator=(const ContainerReader&) = delete;
    ContainerReader(ContainerReader&&) noexcept;
    ContainerReader& operator=(ContainerReader&&) noexcept;
    ~ContainerReader() noexcept;

    const ContainerHeader& header() const noexcept { return header_; }

    bool hasNextBlock() const;
    ContainerBlock readNextBlock();

    // Verifies that a final block was seen and that no bytes follow it.
    void finish();

private:
    ContainerReader(std::string path,
                    std::ifstream&& input,
                    std::uint64_t fileSize,
                    ContainerHeader header);

    std::string path_;
    std::ifstream input_;
    std::uint64_t fileSize_ = 0;
    ContainerHeader header_{};
    bool lastBlockSeen_ = false;
    bool finished_ = false;
};

// Convenience APIs for callers that already have a bounded vector of blocks.
// The streaming classes above are preferred by the block-wise pipeline.
void write_container(const std::string& path,
                     std::uint8_t log2Window,
                     const std::vector<ContainerBlock>& blocks);

struct ContainerFile {
    ContainerHeader header;
    std::vector<ContainerBlock> blocks;
};

ContainerFile read_container(const std::string& path);

} // namespace cmp2

