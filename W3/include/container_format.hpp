#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace cmp2 {

// CMP2 is deliberately a different wire format from the earlier single-payload
// CMP1 proposal.  Do not silently accept or produce CMP1 with this API.
constexpr char kMagic[4] = {'C', 'M', 'P', '2'};
constexpr uint8_t kSupportedVersion = 0x01;
constexpr uint8_t kMaxWindowLog2 = 15; // 2^15 = 32 KiB

// The block-size limit is part of the block-wise design.  It prevents a
// malformed file from requesting an unbounded allocation in the reader.
constexpr uint64_t kMaxSourceBytesPerBlock = 1ULL << 20; // 1 MiB
constexpr uint64_t kMaxPayloadBytesPerBlock = 16ULL << 20; // 16 MiB

enum class BlockType : uint8_t {
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

class ContainerError final : public runtime_error {
public:
    ContainerError(ErrorCode code, const string& message);

    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

struct ContainerHeader {
    uint8_t version = kSupportedVersion;
    uint8_t log2Window = 15;
};

// This is the public handoff object between the pipeline and the container.
// crc32 is supplied by the CRC32 owner; this module only serializes it and
// returns it when reading.
struct ContainerBlock {
    BlockType type = BlockType::Stored;
    bool isLast = false;
    uint64_t sourceLength = 0;
    uint64_t payloadBits = 0;
    vector<uint8_t> payload;
    uint32_t crc32 = 0;
};

class ContainerWriter final {
public:
    static ContainerWriter open(const string& path,
                                uint8_t log2Window);

    ContainerWriter(const ContainerWriter&) = delete;
    ContainerWriter& operator=(const ContainerWriter&) = delete;
    ContainerWriter(ContainerWriter&&) noexcept;
    ContainerWriter& operator=(ContainerWriter&&) noexcept;
    ~ContainerWriter() noexcept;

    const ContainerHeader& header() const noexcept { return header_; }

    // Writes exactly one framed block.  The payload must already be complete,
    // so payloadBytes and payloadBits can be serialized before the payload.
    void writeBlock(const ContainerBlock& block);
    void writeBlock(BlockType type,
                    bool isLast,
                    uint64_t sourceLength,
                    uint64_t payloadBits,
                    const uint8_t* data,
                    size_t size,
                    uint32_t crc32);

    // A valid CMP2 file must contain one block marked isLast=true.
    void finish();

private:
    ContainerWriter(string path,
                    uint8_t log2Window,
                    ofstream&& output);

    string path_;
    ofstream output_;
    ContainerHeader header_{};
    bool lastBlockWritten_ = false;
    bool finished_ = false;
};

class ContainerReader final {
public:
    static ContainerReader open(const string& path);

    ContainerReader(const ContainerReader&) = delete;
    ContainerReader& operator=(const ContainerReader&) = delete;
    ContainerReader(ContainerReader&&) noexcept;
    ContainerReader& operator=(ContainerReader&&) noexcept;
    ~ContainerReader() noexcept;

    const ContainerHeader& header() const noexcept { return header_; }

    bool hasNextBlock() const;
    ContainerBlock readNextBlock();
    ContainerBlock readNextBlock(vector<uint8_t>& reusablePayload);

    // Verifies that a final block was seen and that no bytes follow it.
    void finish();

private:
    ContainerReader(string path,
                    ifstream&& input,
                    uint64_t fileSize,
                    ContainerHeader header);

    string path_;
    ifstream input_;
    uint64_t fileSize_ = 0;
    ContainerHeader header_{};
    bool lastBlockSeen_ = false;
    bool finished_ = false;
};

// Convenience APIs for callers that already have a bounded vector of blocks.
// The streaming classes above are preferred by the block-wise pipeline.
void write_container(const string& path,
                     uint8_t log2Window,
                     const vector<ContainerBlock>& blocks);

struct ContainerFile {
    ContainerHeader header;
    vector<ContainerBlock> blocks;
};

ContainerFile read_container(const string& path);

} // namespace cmp2

