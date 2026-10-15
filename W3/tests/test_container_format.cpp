#include "container_format.hpp"

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

const std::filesystem::path kTestDirectory =
    std::filesystem::temp_directory_path() / "cmp2_container_tests";

std::filesystem::path test_path(const std::string& name) {
    return kTestDirectory / name;
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    assert(output);
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(size);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    assert(input || bytes.empty());
    return bytes;
}

void expect_error(ErrorCode expected, const std::function<void()>& operation) {
    try {
        operation();
        assert(false && "expected ContainerError");
    } catch (const ContainerError& error) {
        assert(error.code() == expected);
    }
}

ContainerBlock stored_block(bool last = true) {
    ContainerBlock block;
    block.type = BlockType::Stored;
    block.isLast = last;
    block.payload = {0x00, 0x01, 0x02, 0xFF};
    block.sourceLength = block.payload.size();
    block.payloadBits = block.payload.size() * 8ULL;
    block.crc32 = 0x12345678U; // supplied by the external CRC32 module in production
    return block;
}

ContainerBlock compressed_block(bool last = true) {
    ContainerBlock block;
    block.type = BlockType::Compressed;
    block.isLast = last;
    block.sourceLength = 37;
    block.payload = {0xA5, 0x80};
    block.payloadBits = 13;
    block.crc32 = 0xCAFEBABEU;
    return block;
}

void test_round_trip_stored_and_compressed() {
    const auto path = test_path("round_trip.cmp2");
    const auto first = stored_block(false);
    const auto second = compressed_block(true);

    cmp2::write_container(path.string(), 15, {first, second});
    const auto file = cmp2::read_container(path.string());

    assert(file.header.version == cmp2::kSupportedVersion);
    assert(file.header.log2Window == 15);
    assert(file.blocks.size() == 2);
    assert(file.blocks[0].type == BlockType::Stored);
    assert(!file.blocks[0].isLast);
    assert(file.blocks[0].payload == first.payload);
    assert(file.blocks[0].crc32 == first.crc32);
    assert(file.blocks[1].type == BlockType::Compressed);
    assert(file.blocks[1].isLast);
    assert(file.blocks[1].payload == second.payload);
    assert(file.blocks[1].payloadBits == second.payloadBits);
    assert(file.blocks[1].crc32 == second.crc32);
}

void test_empty_file_representation() {
    const auto path = test_path("empty.cmp2");
    ContainerBlock block;
    block.type = BlockType::Stored;
    block.isLast = true;
    block.crc32 = 0;

    cmp2::write_container(path.string(), 12, {block});
    const auto file = cmp2::read_container(path.string());

    assert(file.blocks.size() == 1);
    assert(file.blocks[0].sourceLength == 0);
    assert(file.blocks[0].payload.empty());
    assert(file.blocks[0].payloadBits == 0);
}

void test_streaming_api() {
    const auto path = test_path("streaming.cmp2");
    cmp2::ContainerWriter writer = cmp2::ContainerWriter::open(path.string(), 15);
    writer.writeBlock(stored_block(false));
    writer.writeBlock(compressed_block(true));
    writer.finish();

    cmp2::ContainerReader reader = cmp2::ContainerReader::open(path.string());
    assert(reader.hasNextBlock());
    assert(!reader.readNextBlock().isLast);
    assert(reader.hasNextBlock());
    assert(reader.readNextBlock().isLast);
    assert(!reader.hasNextBlock());
    reader.finish();
}

void test_zero_copy_and_reusable_payload_api() {
    const auto path = test_path("reusable_payload.cmp2");
    const std::vector<std::uint8_t> firstPayload{0x10, 0x20, 0x30, 0x40};
    const std::vector<std::uint8_t> secondPayload{0xA5, 0x5A};

    cmp2::ContainerWriter writer = cmp2::ContainerWriter::open(path.string(), 15);
    writer.writeBlock(BlockType::Stored, false, firstPayload.size(),
                      firstPayload.size() * 8ULL, firstPayload.data(),
                      firstPayload.size(), 0x11111111U);
    writer.writeBlock(BlockType::Stored, true, secondPayload.size(),
                      secondPayload.size() * 8ULL, secondPayload.data(),
                      secondPayload.size(), 0x22222222U);
    writer.finish();

    cmp2::ContainerReader reader = cmp2::ContainerReader::open(path.string());
    std::vector<std::uint8_t> reusablePayload;
    auto first = reader.readNextBlock(reusablePayload);
    assert(first.payload.empty());
    assert(reusablePayload == firstPayload);
    const auto* originalBuffer = reusablePayload.data();
    const size_t originalCapacity = reusablePayload.capacity();

    auto second = reader.readNextBlock(reusablePayload);
    assert(second.payload.empty());
    assert(reusablePayload == secondPayload);
    assert(reusablePayload.data() == originalBuffer);
    assert(reusablePayload.capacity() == originalCapacity);
    reader.finish();
}

void test_bad_signature() {
    const auto path = test_path("bad_signature.cmp2");
    auto bytes = read_bytes(test_path("round_trip.cmp2"));
    bytes[0] = 'X';
    write_bytes(path, bytes);
    expect_error(ErrorCode::InvalidSignature,
                 [&] { cmp2::ContainerReader::open(path.string()); });
}

void test_unsupported_version() {
    const auto path = test_path("bad_version.cmp2");
    auto bytes = read_bytes(test_path("round_trip.cmp2"));
    bytes[4] = 0x7F;
    write_bytes(path, bytes);
    expect_error(ErrorCode::UnsupportedVersion,
                 [&] { cmp2::ContainerReader::open(path.string()); });
}

void test_truncated_input() {
    const auto path = test_path("truncated.cmp2");
    auto bytes = read_bytes(test_path("round_trip.cmp2"));
    bytes.resize(bytes.size() - 2);
    write_bytes(path, bytes);
    expect_error(ErrorCode::TruncatedInput,
                 [&] { cmp2::read_container(path.string()); });
}

void test_trailing_data() {
    const auto path = test_path("trailing_data.cmp2");
    auto bytes = read_bytes(test_path("round_trip.cmp2"));
    bytes.push_back(0xEE);
    write_bytes(path, bytes);
    expect_error(ErrorCode::TrailingData,
                 [&] { cmp2::read_container(path.string()); });
}

void test_invalid_bit_length() {
    const auto path = test_path("invalid_bits.cmp2");
    auto bytes = read_bytes(test_path("round_trip.cmp2"));

    // The second block starts after the 6-byte file header and the first
    // block.  Locate the payloadBits varint by using the writer's known small
    // layout: first block is 1 + 1 + 1 + 1 + 4 + 4 bytes.
    const std::size_t secondBlockOffset = 6 + 12;
    const std::size_t payloadBitsOffset = secondBlockOffset + 1 + 1 + 1;
    bytes[payloadBitsOffset] = 0x7F; // 127 bits for a two-byte payload
    write_bytes(path, bytes);

    expect_error(ErrorCode::InvalidBitLength,
                 [&] { cmp2::read_container(path.string()); });
}

void test_writer_rejects_missing_final_block() {
    const auto path = test_path("missing_final_writer.cmp2");
    expect_error(ErrorCode::MissingFinalBlock, [&] {
        cmp2::ContainerWriter writer = cmp2::ContainerWriter::open(
            path.string(), 15);
        writer.writeBlock(stored_block(false));
        writer.finish();
    });
}

void test_writer_rejects_invalid_stored_block() {
    const auto path = test_path("invalid_stored_writer.cmp2");
    expect_error(ErrorCode::InvalidBitLength, [&] {
        cmp2::ContainerBlock block = stored_block(true);
        block.payloadBits = 1;
        cmp2::write_container(path.string(), 15, {block});
    });
}

void test_reserved_flags() {
    const auto path = test_path("reserved_flags.cmp2");
    auto bytes = read_bytes(test_path("round_trip.cmp2"));
    bytes[6] = static_cast<std::uint8_t>(bytes[6] | 0x80U);
    write_bytes(path, bytes);
    expect_error(ErrorCode::InvalidFlags,
                 [&] { cmp2::read_container(path.string()); });
}

} // namespace

int main() {
    std::filesystem::remove_all(kTestDirectory);
    std::filesystem::create_directories(kTestDirectory);

    test_round_trip_stored_and_compressed();
    test_empty_file_representation();
    test_streaming_api();
    test_zero_copy_and_reusable_payload_api();
    test_bad_signature();
    test_unsupported_version();
    test_truncated_input();
    test_trailing_data();
    test_invalid_bit_length();
    test_writer_rejects_missing_final_block();
    test_writer_rejects_invalid_stored_block();
    test_reserved_flags();

    std::filesystem::remove_all(kTestDirectory);
    std::cout << "All CMP2 container tests passed\n";
    return 0;
}

