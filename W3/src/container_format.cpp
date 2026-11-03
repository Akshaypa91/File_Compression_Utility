#include "container_format.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

using namespace std;

namespace cmp2 {
namespace {

constexpr uint8_t kLastFlag = 0x01;
constexpr uint8_t kTypeMask = 0x06;
constexpr uint8_t kReservedMask = 0xF8;

string make_message(ErrorCode code, const string& detail) {
    ostringstream message;
    message << error_code_name(code);
    if (!detail.empty()) {
        message << ": " << detail;
    }
    return message.str();
}

[[noreturn]] void fail(ErrorCode code, const string& detail) {
    throw ContainerError(code, make_message(code, detail));
}

void validate_window(uint8_t log2Window) {
    if (log2Window > kMaxWindowLog2) {
        fail(ErrorCode::InvalidWindow,
             "log2Window exceeds the 32 KiB maximum");
    }
}

uint64_t current_position(istream& input) {
    const streampos position = input.tellg();
    if (position < 0) {
        fail(ErrorCode::FileReadFailed, "unable to determine file position");
    }
    return static_cast<uint64_t>(position);
}

void ensure_can_read(ifstream& input,
                     uint64_t fileSize,
                     uint64_t bytes,
                     const string& field) {
    const streampos position = input.tellg();
    if (position < 0) {
        fail(ErrorCode::FileReadFailed, "unable to determine file position");
    }

    const auto offset = static_cast<uint64_t>(position);
    if (offset > fileSize || bytes > fileSize - offset) {
        fail(ErrorCode::TruncatedInput,
             field + " extends beyond the end of the file");
    }
}

void read_exact(ifstream& input,
                uint64_t fileSize,
                uint8_t* destination,
                size_t size,
                const string& field) {
    ensure_can_read(input, fileSize, static_cast<uint64_t>(size), field);
    if (size == 0) {
        return;
    }

    input.read(reinterpret_cast<char*>(destination),
               static_cast<streamsize>(size));
    if (!input) {
        fail(ErrorCode::FileReadFailed, "failed while reading " + field);
    }
}

uint8_t read_byte(ifstream& input,
                       uint64_t fileSize,
                       const string& field) {
    uint8_t byte = 0;
    read_exact(input, fileSize, &byte, 1, field);
    return byte;
}

void write_exact(ofstream& output,
                 const uint8_t* data,
                 size_t size,
                 const string& field) {
    if (size == 0) {
        return;
    }

    output.write(reinterpret_cast<const char*>(data),
                 static_cast<streamsize>(size));
    if (!output) {
        fail(ErrorCode::FileWriteFailed, "failed while writing " + field);
    }
}

void write_byte(ofstream& output,
                uint8_t value,
                const string& field) {
    write_exact(output, &value, 1, field);
}

void write_uint32_le(ofstream& output,
                     uint32_t value,
                     const string& field) {
    const array<uint8_t, 4> bytes{
        static_cast<uint8_t>(value & 0xFFU),
        static_cast<uint8_t>((value >> 8U) & 0xFFU),
        static_cast<uint8_t>((value >> 16U) & 0xFFU),
        static_cast<uint8_t>((value >> 24U) & 0xFFU),
    };
    write_exact(output, bytes.data(), bytes.size(), field);
}

uint32_t read_uint32_le(ifstream& input,
                             uint64_t fileSize,
                             const string& field) {
    array<uint8_t, 4> bytes{};
    read_exact(input, fileSize, bytes.data(), bytes.size(), field);

    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) |
           (static_cast<uint32_t>(bytes[3]) << 24U);
}

void write_varint(ofstream& output, uint64_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0) {
            byte = static_cast<uint8_t>(byte | 0x80U);
        }
        write_byte(output, byte, "varint");
    } while (value != 0);
}

uint64_t read_varint(ifstream& input,
                          uint64_t fileSize,
                          const string& field) {
    uint64_t value = 0;

    for (size_t index = 0; index < 10; ++index) {
        const uint8_t byte = read_byte(input, fileSize, field);
        const uint8_t data = static_cast<uint8_t>(byte & 0x7FU);

        if (index == 9 && data > 1U) {
            fail(ErrorCode::CorruptHeader, field + " overflows uint64_t");
        }

        value |= static_cast<uint64_t>(data) << (index * 7U);

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

uint8_t make_flags(BlockType type, bool isLast) {
    const auto typeValue = static_cast<uint8_t>(type);
    return static_cast<uint8_t>((isLast ? kLastFlag : 0U) |
                                     ((typeValue & 0x03U) << 1U));
}

void validate_block_for_write(BlockType type,
                              uint64_t sourceLength,
                              uint64_t payloadBits,
                              const uint8_t* data,
                              size_t size) {
    if (sourceLength > kMaxSourceBytesPerBlock) {
        fail(ErrorCode::InvalidBlock,
             "sourceLength exceeds the per-block limit");
    }
    if (size > kMaxPayloadBytesPerBlock) {
        fail(ErrorCode::InvalidBlock,
             "payload exceeds the per-block limit");
    }
    if (size != 0 && data == nullptr) {
        fail(ErrorCode::InvalidArgument, "payload data is null");
    }

    const auto payloadBytes = static_cast<uint64_t>(size);
    if (payloadBits > payloadBytes * 8ULL) {
        fail(ErrorCode::InvalidBitLength,
             "payloadBits is greater than payloadBytes * 8");
    }

    switch (type) {
    case BlockType::Stored:
        if (sourceLength != payloadBytes) {
            fail(ErrorCode::InvalidBlock,
                 "stored sourceLength must equal payloadBytes");
        }
        if (payloadBits != payloadBytes * 8ULL) {
            fail(ErrorCode::InvalidBitLength,
                 "stored payloadBits must equal payloadBytes * 8");
        }
        break;
    case BlockType::Compressed:
        if (sourceLength == 0 || size == 0 || payloadBits == 0) {
            fail(ErrorCode::InvalidBlock,
                 "compressed blocks must contain source and payload data");
        }
        break;
    default:
        fail(ErrorCode::InvalidBlockType, "unknown block type");
    }
}

ContainerBlock parse_block(ifstream& input,
                           uint64_t fileSize,
                           bool previousBlockWasLast,
                           vector<uint8_t>& reusablePayload) {
    if (previousBlockWasLast) {
        fail(ErrorCode::InvalidBlockOrder,
             "cannot read a block after the final block");
    }

    const uint8_t flags = read_byte(input, fileSize, "block flags");
    if ((flags & kReservedMask) != 0) {
        fail(ErrorCode::InvalidFlags,
             "reserved block flag bits must be zero");
    }

    const auto typeValue = static_cast<uint8_t>((flags & kTypeMask) >> 1U);
    if (typeValue > static_cast<uint8_t>(BlockType::Compressed)) {
        fail(ErrorCode::InvalidBlockType, "unknown block type");
    }

    ContainerBlock block;
    block.type = static_cast<BlockType>(typeValue);
    block.isLast = (flags & kLastFlag) != 0;
    block.sourceLength = read_varint(input, fileSize, "sourceLength");
    const uint64_t payloadBytes =
        read_varint(input, fileSize, "payloadBytes");
    block.payloadBits = read_varint(input, fileSize, "payloadBits");

    if (block.sourceLength > kMaxSourceBytesPerBlock) {
        fail(ErrorCode::CorruptHeader,
             "sourceLength exceeds the per-block limit");
    }
    if (payloadBytes > kMaxPayloadBytesPerBlock ||
        payloadBytes > static_cast<uint64_t>(
                            numeric_limits<size_t>::max())) {
        fail(ErrorCode::CorruptHeader,
             "payloadBytes exceeds the per-block limit");
    }
    if (block.payloadBits > payloadBytes * 8ULL) {
        fail(ErrorCode::InvalidBitLength,
             "payloadBits is greater than payloadBytes * 8");
    }

    ensure_can_read(input, fileSize, payloadBytes + 4ULL,
                    "payload and block CRC32");

    reusablePayload.resize(static_cast<size_t>(payloadBytes));
    read_exact(input, fileSize, reusablePayload.data(), reusablePayload.size(),
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

ContainerError::ContainerError(ErrorCode code, const string& message)
    : runtime_error(message), code_(code) {}

ContainerWriter::ContainerWriter(string path,
                                 uint8_t log2Window,
                                 ofstream&& output)
    : path_(move(path)), output_(move(output)) {
    header_.version = kSupportedVersion;
    header_.log2Window = log2Window;
}

ContainerWriter ContainerWriter::open(const string& path,
                                      uint8_t log2Window) {
    validate_window(log2Window);

    ofstream output(path, ios::binary | ios::trunc);
    if (!output) {
        fail(ErrorCode::FileOpenFailed, "unable to open output file: " + path);
    }

    ContainerWriter writer(path, log2Window, move(output));
    write_exact(writer.output_,
                reinterpret_cast<const uint8_t*>(kMagic),
                sizeof(kMagic),
                "file signature");
    write_byte(writer.output_, kSupportedVersion, "file version");
    write_byte(writer.output_, log2Window, "window size");
    return writer;
}

ContainerWriter::ContainerWriter(ContainerWriter&& other) noexcept
    : path_(move(other.path_)),
      output_(move(other.output_)),
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
        path_ = move(other.path_);
        output_ = move(other.output_);
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
    writeBlock(block.type, block.isLast, block.sourceLength, block.payloadBits,
               block.payload.data(), block.payload.size(), block.crc32);
}

void ContainerWriter::writeBlock(BlockType type,
                                 bool isLast,
                                 uint64_t sourceLength,
                                 uint64_t payloadBits,
                                 const uint8_t* data,
                                 size_t size,
                                 uint32_t crc32) {
    if (finished_) {
        fail(ErrorCode::InvalidBlockOrder,
             "cannot write a block after finish()");
    }
    if (lastBlockWritten_) {
        fail(ErrorCode::InvalidBlockOrder,
             "cannot write a block after the final block");
    }
    validate_block_for_write(type, sourceLength, payloadBits, data, size);

    const auto flags = make_flags(type, isLast);
    write_byte(output_, flags, "block flags");
    write_varint(output_, sourceLength);
    write_varint(output_, static_cast<uint64_t>(size));
    write_varint(output_, payloadBits);
    write_exact(output_, data, size, "block payload");
    write_uint32_le(output_, crc32, "block CRC32");

    if (isLast) {
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

ContainerReader::ContainerReader(string path,
                                 ifstream&& input,
                                 uint64_t fileSize,
                                 ContainerHeader header)
    : path_(move(path)),
      input_(move(input)),
      fileSize_(fileSize),
      header_(header) {}

ContainerReader ContainerReader::open(const string& path) {
    ifstream input(path, ios::binary);
    if (!input) {
        fail(ErrorCode::FileOpenFailed, "unable to open input file: " + path);
    }

    input.seekg(0, ios::end);
    const streampos end = input.tellg();
    if (end < 0) {
        fail(ErrorCode::FileReadFailed, "unable to determine input file size");
    }
    const auto fileSize = static_cast<uint64_t>(end);
    input.seekg(0, ios::beg);

    array<uint8_t, sizeof(kMagic)> magic{};
    read_exact(input, fileSize, magic.data(), magic.size(), "file signature");
    for (size_t index = 0; index < magic.size(); ++index) {
        if (magic[index] != static_cast<uint8_t>(kMagic[index])) {
            fail(ErrorCode::InvalidSignature, "expected CMP2");
        }
    }

    const auto version = read_byte(input, fileSize, "file version");
    if (version != kSupportedVersion) {
        fail(ErrorCode::UnsupportedVersion,
             "unsupported CMP2 version " + to_string(version));
    }

    const auto log2Window = read_byte(input, fileSize, "window size");
    validate_window(log2Window);

    return ContainerReader(path, move(input), fileSize,
                           ContainerHeader{version, log2Window});
}

ContainerReader::ContainerReader(ContainerReader&& other) noexcept
    : path_(move(other.path_)),
      input_(move(other.input_)),
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
        path_ = move(other.path_);
        input_ = move(other.input_);
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
    const auto position = current_position(const_cast<ifstream&>(input_));
    return position < fileSize_;
}

ContainerBlock ContainerReader::readNextBlock() {
    vector<uint8_t> reusablePayload;
    ContainerBlock block = readNextBlock(reusablePayload);
    block.payload = move(reusablePayload);
    return block;
}

ContainerBlock ContainerReader::readNextBlock(
    vector<uint8_t>& reusablePayload) {
    if (finished_ || lastBlockSeen_) {
        fail(ErrorCode::InvalidBlockOrder,
             "cannot read a block after the final block");
    }
    if (!hasNextBlock()) {
        fail(ErrorCode::MissingFinalBlock,
             "input ended before a final block was found");
    }

    ContainerBlock block = parse_block(input_, fileSize_, lastBlockSeen_,
                                       reusablePayload);
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

void write_container(const string& path,
                     uint8_t log2Window,
                     const vector<ContainerBlock>& blocks) {
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

ContainerFile read_container(const string& path) {
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

