#include "container_format.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace cmp2 {
namespace {

constexpr std::uint8_t kLastFlag = 0x01;
constexpr std::uint8_t kTypeMask = 0x06;
constexpr std::uint8_t kReservedMask = 0xF8;

std::string make_message(ErrorCode code, const std::string& detail) {
    std::ostringstream message;
    message << error_code_name(code);
    if (!detail.empty()) {
        message << ": " << detail;
    }
    return message.str();
}

[[noreturn]] void fail(ErrorCode code, const std::string& detail) {
    throw ContainerError(code, make_message(code, detail));
}

void validate_window(std::uint8_t log2Window) {
    if (log2Window > kMaxWindowLog2) {
        fail(ErrorCode::InvalidWindow,
             "log2Window exceeds the 32 KiB maximum");
    }
}

std::uint64_t current_position(std::istream& input) {
    const std::streampos position = input.tellg();
    if (position < 0) {
        fail(ErrorCode::FileReadFailed, "unable to determine file position");
    }
    return static_cast<std::uint64_t>(position);
}

void ensure_can_read(std::ifstream& input,
                     std::uint64_t fileSize,
                     std::uint64_t bytes,
                     const std::string& field) {
    const std::streampos position = input.tellg();
    if (position < 0) {
        fail(ErrorCode::FileReadFailed, "unable to determine file position");
    }

    const auto offset = static_cast<std::uint64_t>(position);
    if (offset > fileSize || bytes > fileSize - offset) {
        fail(ErrorCode::TruncatedInput,
             field + " extends beyond the end of the file");
    }
}

void read_exact(std::ifstream& input,
                std::uint64_t fileSize,
                std::uint8_t* destination,
                std::size_t size,
                const std::string& field) {
    ensure_can_read(input, fileSize, static_cast<std::uint64_t>(size), field);
    if (size == 0) {
        return;
    }

    input.read(reinterpret_cast<char*>(destination),
               static_cast<std::streamsize>(size));
    if (!input) {
        fail(ErrorCode::FileReadFailed, "failed while reading " + field);
    }
}

std::uint8_t read_byte(std::ifstream& input,
                       std::uint64_t fileSize,
                       const std::string& field) {
    std::uint8_t byte = 0;
    read_exact(input, fileSize, &byte, 1, field);
    return byte;
}

void write_exact(std::ofstream& output,
                 const std::uint8_t* data,
                 std::size_t size,
                 const std::string& field) {
    if (size == 0) {
        return;
    }

    output.write(reinterpret_cast<const char*>(data),
                 static_cast<std::streamsize>(size));
    if (!output) {
        fail(ErrorCode::FileWriteFailed, "failed while writing " + field);
    }
}

void write_byte(std::ofstream& output,
                std::uint8_t value,
                const std::string& field) {
    write_exact(output, &value, 1, field);
}

void write_uint32_le(std::ofstream& output,
                     std::uint32_t value,
                     const std::string& field) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
    };
    write_exact(output, bytes.data(), bytes.size(), field);
}

std::uint32_t read_uint32_le(std::ifstream& input,
                             std::uint64_t fileSize,
                             const std::string& field) {
    std::array<std::uint8_t, 4> bytes{};
    read_exact(input, fileSize, bytes.data(), bytes.size(), field);

    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

void write_varint(std::ofstream& output, std::uint64_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0) {
            byte = static_cast<std::uint8_t>(byte | 0x80U);
        }
        write_byte(output, byte, "varint");
    } while (value != 0);
}

std::uint64_t read_varint(std::ifstream& input,
                          std::uint64_t fileSize,
                          const std::string& field) {
    std::uint64_t value = 0;

    for (std::size_t index = 0; index < 10; ++index) {
        const std::uint8_t byte = read_byte(input, fileSize, field);
        const std::uint8_t data = static_cast<std::uint8_t>(byte & 0x7FU);

        if (index == 9 && data > 1U) {
            fail(ErrorCode::CorruptHeader, field + " overflows uint64_t");
        }

        value |= static_cast<std::uint64_t>(data) << (index * 7U);

        if ((byte & 0x80U) == 0) {
            // Reject non-canonical encodings such as 0x80 0x00.  This keeps
            // one logical value represented by exactly one byte sequence.
            if (index > 0 && data == 0) {
                fail(ErrorCode::CorruptHeader,
                     field + " uses a non-canonical varint");
            }
            return value;
        }
    }

    fail(ErrorCode::CorruptHeader, field + " is an unterminated varint");
}

std::uint8_t make_flags(const ContainerBlock& block) {
    const auto type = static_cast<std::uint8_t>(block.type);
    return static_cast<std::uint8_t>((block.isLast ? kLastFlag : 0U) |
                                     ((type & 0x03U) << 1U));
}

void validate_block_for_write(const ContainerBlock& block) {
    if (block.sourceLength > kMaxSourceBytesPerBlock) {
        fail(ErrorCode::InvalidBlock,
             "sourceLength exceeds the per-block limit");
    }
    if (block.payload.size() > kMaxPayloadBytesPerBlock) {
        fail(ErrorCode::InvalidBlock,
             "payload exceeds the per-block limit");
    }

    const auto payloadBytes = static_cast<std::uint64_t>(block.payload.size());
    if (block.payloadBits > payloadBytes * 8ULL) {
        fail(ErrorCode::InvalidBitLength,
             "payloadBits is greater than payloadBytes * 8");
    }

    switch (block.type) {
    case BlockType::Stored:
        if (block.sourceLength != payloadBytes) {
            fail(ErrorCode::InvalidBlock,
                 "stored sourceLength must equal payloadBytes");
        }
        if (block.payloadBits != payloadBytes * 8ULL) {
            fail(ErrorCode::InvalidBitLength,
                 "stored payloadBits must equal payloadBytes * 8");
        }
        break;
    case BlockType::Compressed:
        if (block.sourceLength == 0 || block.payload.empty() ||
            block.payloadBits == 0) {
            fail(ErrorCode::InvalidBlock,
                 "compressed blocks must contain source and payload data");
        }
        break;
    default:
        fail(ErrorCode::InvalidBlockType, "unknown block type");
    }
}

ContainerBlock parse_block(std::ifstream& input,
                           std::uint64_t fileSize,
                           bool previousBlockWasLast) {
    if (previousBlockWasLast) {
        fail(ErrorCode::InvalidBlockOrder,
             "cannot read a block after the final block");
    }

    const std::uint8_t flags = read_byte(input, fileSize, "block flags");
    if ((flags & kReservedMask) != 0) {
        fail(ErrorCode::InvalidFlags,
             "reserved block flag bits must be zero");
    }

    const auto typeValue = static_cast<std::uint8_t>((flags & kTypeMask) >> 1U);
    if (typeValue > static_cast<std::uint8_t>(BlockType::Compressed)) {
        fail(ErrorCode::InvalidBlockType, "unknown block type");
    }

    ContainerBlock block;
    block.type = static_cast<BlockType>(typeValue);
    block.isLast = (flags & kLastFlag) != 0;
    block.sourceLength = read_varint(input, fileSize, "sourceLength");
    const std::uint64_t payloadBytes =
        read_varint(input, fileSize, "payloadBytes");
    block.payloadBits = read_varint(input, fileSize, "payloadBits");

    if (block.sourceLength > kMaxSourceBytesPerBlock) {
        fail(ErrorCode::CorruptHeader,
             "sourceLength exceeds the per-block limit");
    }
    if (payloadBytes > kMaxPayloadBytesPerBlock ||
        payloadBytes > static_cast<std::uint64_t>(
                            std::numeric_limits<std::size_t>::max())) {
        fail(ErrorCode::CorruptHeader,
             "payloadBytes exceeds the per-block limit");
    }
    if (block.payloadBits > payloadBytes * 8ULL) {
        fail(ErrorCode::InvalidBitLength,
             "payloadBits is greater than payloadBytes * 8");
    }

    ensure_can_read(input, fileSize, payloadBytes + 4ULL,
                    "payload and block CRC32");

    block.payload.resize(static_cast<std::size_t>(payloadBytes));
    read_exact(input, fileSize, block.payload.data(), block.payload.size(),
               "block payload");
    block.crc32 = read_uint32_le(input, fileSize, "block CRC32");

    if (block.type == BlockType::Stored) {
        if (block.sourceLength != payloadBytes) {
            fail(ErrorCode::InvalidBlock,
                 "stored sourceLength must equal payloadBytes");
        }
        if (block.payloadBits != payloadBytes * 8ULL) {
            fail(ErrorCode::InvalidBitLength,
                 "stored payloadBits must equal payloadBytes * 8");
        }
    } else if (block.sourceLength == 0 || payloadBytes == 0 ||
               block.payloadBits == 0) {
        fail(ErrorCode::InvalidBlock,
             "compressed blocks must contain source and payload data");
    }

    return block;
}

} // namespace

const char* error_code_name(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::FileOpenFailed:
        return "FILE_OPEN_FAILED";
    case ErrorCode::FileReadFailed:
        return "FILE_READ_FAILED";
    case ErrorCode::FileWriteFailed:
        return "FILE_WRITE_FAILED";
    case ErrorCode::InvalidArgument:
        return "INVALID_ARGUMENT";
    case ErrorCode::InvalidSignature:
        return "INVALID_SIGNATURE";
    case ErrorCode::UnsupportedVersion:
        return "UNSUPPORTED_VERSION";
    case ErrorCode::InvalidWindow:
        return "INVALID_WINDOW";
    case ErrorCode::TruncatedInput:
        return "TRUNCATED_INPUT";
    case ErrorCode::CorruptHeader:
        return "CORRUPT_HEADER";
    case ErrorCode::InvalidFlags:
        return "INVALID_FLAGS";
    case ErrorCode::InvalidBlockType:
        return "INVALID_BLOCK_TYPE";
    case ErrorCode::InvalidBitLength:
        return "INVALID_BIT_LENGTH";
    case ErrorCode::InvalidBlock:
        return "INVALID_BLOCK";
    case ErrorCode::InvalidBlockOrder:
        return "INVALID_BLOCK_ORDER";
    case ErrorCode::MissingFinalBlock:
        return "MISSING_FINAL_BLOCK";
    case ErrorCode::TrailingData:
        return "TRAILING_DATA";
    }
    return "UNKNOWN_CONTAINER_ERROR";
}

ContainerError::ContainerError(ErrorCode code, const std::string& message)
    : std::runtime_error(message), code_(code) {}

ContainerWriter::ContainerWriter(std::string path,
                                 std::uint8_t log2Window,
                                 std::ofstream&& output)
    : path_(std::move(path)), output_(std::move(output)) {
    header_.version = kSupportedVersion;
    header_.log2Window = log2Window;
}

ContainerWriter ContainerWriter::open(const std::string& path,
                                      std::uint8_t log2Window) {
    validate_window(log2Window);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        fail(ErrorCode::FileOpenFailed, "unable to open output file: " + path);
    }

    ContainerWriter writer(path, log2Window, std::move(output));
    write_exact(writer.output_,
                reinterpret_cast<const std::uint8_t*>(kMagic),
                sizeof(kMagic),
                "file signature");
    write_byte(writer.output_, kSupportedVersion, "file version");
    write_byte(writer.output_, log2Window, "window size");
    return writer;
}

ContainerWriter::ContainerWriter(ContainerWriter&& other) noexcept
    : path_(std::move(other.path_)),
      output_(std::move(other.output_)),
      header_(other.header_),
      lastBlockWritten_(other.lastBlockWritten_),
      finished_(other.finished_) {
    other.finished_ = true;
}

ContainerWriter& ContainerWriter::operator=(ContainerWriter&& other) noexcept {
    if (this != &other) {
        if (output_.is_open()) {
            output_.close();
        }
        path_ = std::move(other.path_);
        output_ = std::move(other.output_);
        header_ = other.header_;
        lastBlockWritten_ = other.lastBlockWritten_;
        finished_ = other.finished_;
        other.finished_ = true;
    }
    return *this;
}

ContainerWriter::~ContainerWriter() noexcept {
    if (output_.is_open()) {
        output_.close();
    }
}

void ContainerWriter::writeBlock(const ContainerBlock& block) {
    if (finished_) {
        fail(ErrorCode::InvalidBlockOrder,
             "cannot write a block after finish()");
    }
    if (lastBlockWritten_) {
        fail(ErrorCode::InvalidBlockOrder,
             "cannot write a block after the final block");
    }
    validate_block_for_write(block);

    const auto flags = make_flags(block);
    write_byte(output_, flags, "block flags");
    write_varint(output_, block.sourceLength);
    write_varint(output_, static_cast<std::uint64_t>(block.payload.size()));
    write_varint(output_, block.payloadBits);
    write_exact(output_, block.payload.data(), block.payload.size(),
                "block payload");
    write_uint32_le(output_, block.crc32, "block CRC32");

    if (block.isLast) {
        lastBlockWritten_ = true;
    }
}

void ContainerWriter::finish() {
    if (finished_) {
        return;
    }
    if (!lastBlockWritten_) {
        fail(ErrorCode::MissingFinalBlock,
             "finish() requires a block marked as final");
    }

    output_.flush();
    if (!output_) {
        fail(ErrorCode::FileWriteFailed, "failed to flush output file: " + path_);
    }
    output_.close();
    if (output_.fail()) {
        fail(ErrorCode::FileWriteFailed, "failed to close output file: " + path_);
    }
    finished_ = true;
}

ContainerReader::ContainerReader(std::string path,
                                 std::ifstream&& input,
                                 std::uint64_t fileSize,
                                 ContainerHeader header)
    : path_(std::move(path)),
      input_(std::move(input)),
      fileSize_(fileSize),
      header_(header) {}

ContainerReader ContainerReader::open(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        fail(ErrorCode::FileOpenFailed, "unable to open input file: " + path);
    }

    input.seekg(0, std::ios::end);
    const std::streampos end = input.tellg();
    if (end < 0) {
        fail(ErrorCode::FileReadFailed, "unable to determine input file size");
    }
    const auto fileSize = static_cast<std::uint64_t>(end);
    input.seekg(0, std::ios::beg);

    std::array<std::uint8_t, sizeof(kMagic)> magic{};
    read_exact(input, fileSize, magic.data(), magic.size(), "file signature");
    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (magic[index] != static_cast<std::uint8_t>(kMagic[index])) {
            fail(ErrorCode::InvalidSignature, "expected CMP2");
        }
    }

    const auto version = read_byte(input, fileSize, "file version");
    if (version != kSupportedVersion) {
        fail(ErrorCode::UnsupportedVersion,
             "unsupported CMP2 version " + std::to_string(version));
    }

    const auto log2Window = read_byte(input, fileSize, "window size");
    validate_window(log2Window);

    return ContainerReader(path, std::move(input), fileSize,
                           ContainerHeader{version, log2Window});
}

ContainerReader::ContainerReader(ContainerReader&& other) noexcept
    : path_(std::move(other.path_)),
      input_(std::move(other.input_)),
      fileSize_(other.fileSize_),
      header_(other.header_),
      lastBlockSeen_(other.lastBlockSeen_),
      finished_(other.finished_) {
    other.finished_ = true;
}

ContainerReader& ContainerReader::operator=(ContainerReader&& other) noexcept {
    if (this != &other) {
        if (input_.is_open()) {
            input_.close();
        }
        path_ = std::move(other.path_);
        input_ = std::move(other.input_);
        fileSize_ = other.fileSize_;
        header_ = other.header_;
        lastBlockSeen_ = other.lastBlockSeen_;
        finished_ = other.finished_;
        other.finished_ = true;
    }
    return *this;
}

ContainerReader::~ContainerReader() noexcept {
    if (input_.is_open()) {
        input_.close();
    }
}

bool ContainerReader::hasNextBlock() const {
    if (finished_ || lastBlockSeen_) {
        return false;
    }
    const auto position = current_position(const_cast<std::ifstream&>(input_));
    return position < fileSize_;
}

ContainerBlock ContainerReader::readNextBlock() {
    if (finished_ || lastBlockSeen_) {
        fail(ErrorCode::InvalidBlockOrder,
             "cannot read a block after the final block");
    }
    if (!hasNextBlock()) {
        fail(ErrorCode::MissingFinalBlock,
             "input ended before a final block was found");
    }

    ContainerBlock block = parse_block(input_, fileSize_, lastBlockSeen_);
    if (block.isLast) {
        lastBlockSeen_ = true;
        if (current_position(input_) != fileSize_) {
            fail(ErrorCode::TrailingData,
                 "bytes remain after the final block");
        }
    }
    return block;
}

void ContainerReader::finish() {
    if (finished_) {
        return;
    }
    if (!lastBlockSeen_) {
        fail(ErrorCode::MissingFinalBlock,
             "input does not contain a final block");
    }
    if (current_position(input_) != fileSize_) {
        fail(ErrorCode::TrailingData,
             "bytes remain after the final block");
    }
    finished_ = true;
    input_.close();
}

void write_container(const std::string& path,
                     std::uint8_t log2Window,
                     const std::vector<ContainerBlock>& blocks) {
    if (blocks.empty()) {
        fail(ErrorCode::MissingFinalBlock,
             "a CMP2 file must contain at least one block");
    }

    ContainerWriter writer = ContainerWriter::open(path, log2Window);
    for (const auto& block : blocks) {
        writer.writeBlock(block);
    }
    writer.finish();
}

ContainerFile read_container(const std::string& path) {
    ContainerReader reader = ContainerReader::open(path);
    ContainerFile file;
    file.header = reader.header();

    while (reader.hasNextBlock()) {
        file.blocks.push_back(reader.readNextBlock());
    }
    reader.finish();
    return file;
}

} // namespace cmp2

