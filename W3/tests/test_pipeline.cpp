#include "pipeline_controller.hpp"
#include "command_line_interface.hpp"

#include "crc32.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>

using namespace std;
using namespace worklet3;

namespace {

class LiteralBlockProducer final : public W2BlockProducer {
public:
    void encodeBlock(const uint8_t* rawBytes,
                     size_t availableBytes,
                     uint32_t,
                     cmp::TokenBlock& block) override {
        block.reset();
        block.src = rawBytes;
        size_t consumed = 0;
        if (availableBytes >= 4 &&
            all_of(rawBytes, rawBytes + availableBytes,
                   [&](uint8_t byte) { return byte == rawBytes[0]; })) {
            block.addLiteral(rawBytes[0]);
            consumed = 1;
            while (availableBytes - consumed > 1 && block.hasRoomForToken()) {
                const uint32_t length = static_cast<uint32_t>(min<size_t>(
                    cmp::kMaxMatch, availableBytes - consumed - 1));
                block.addToken(cmp::Token{1, length, rawBytes[consumed + length]});
                consumed += length + 1;
            }
        }
        while (consumed < availableBytes && !block.full()) {
            block.addLiteral(rawBytes[consumed++]);
        }
        block.srcLen = static_cast<uint32_t>(consumed);
    }
};

class OutputSink final : public W2DecodeSink {
public:
    void setOutput(ostream& output) override {
        output_ = &output;
        history_.clear();
        produced_ = 0;
    }

    void lit(uint8_t byte) override {
        if (output_ == nullptr) throw runtime_error("Output stream is not set");
        output_->put(static_cast<char>(byte));
        if (!*output_) throw runtime_error("Output write failed");
        remember(byte);
        ++produced_;
    }

    bool match(uint32_t length, uint32_t distance) override {
        if (distance == 0 || distance > history_.size() || distance > cmp::kMaxDistance) {
            return false;
        }
        for (uint32_t index = 0; index < length; ++index) {
            const size_t source = history_.size() < cmp::kMaxDistance
                ? history_.size() - distance
                : (historyPosition_ + cmp::kMaxDistance - distance) % cmp::kMaxDistance;
            const uint8_t byte = history_[source];
            lit(byte);
        }
        return true;
    }

    uint64_t bytesProduced() const override { return produced_; }

private:
    void remember(uint8_t byte) {
        if (history_.size() < cmp::kMaxDistance) {
            history_.push_back(byte);
            historyPosition_ = history_.size() % cmp::kMaxDistance;
        } else {
            history_[historyPosition_] = byte;
            historyPosition_ = (historyPosition_ + 1) % cmp::kMaxDistance;
        }
    }

    ostream* output_ = nullptr;
    vector<uint8_t> history_;
    size_t historyPosition_ = 0;
    uint64_t produced_ = 0;
};

vector<uint8_t> readBytes(const string& path) {
    ifstream input(path, ios::binary);
    return vector<uint8_t>(istreambuf_iterator<char>(input), {});
}

size_t readVarint(const vector<uint8_t>& bytes, size_t& position) {
    uint8_t byte = 0;
    do {
        assert(position < bytes.size());
        byte = bytes[position++];
    } while ((byte & 0x80U) != 0);
    return position;
}

void writeBytes(const string& path, const vector<uint8_t>& bytes) {
    ofstream output(path, ios::binary | ios::trunc);
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<streamsize>(bytes.size()));
    }
    assert(output.good());
}

void roundTrip(PipelineController& pipeline,
               const vector<uint8_t>& inputBytes,
               const string& inputPath,
               const string& archivePath,
               const string& outputPath) {
    writeBytes(inputPath, inputBytes);
    pipeline.compressFile(inputPath, archivePath);
    pipeline.decompressFile(archivePath, outputPath);
    assert(readBytes(outputPath) == inputBytes);
}

void testRoundTripsAndSelection() {
    const string inputPath = "/tmp/w3_pipeline_input.bin";
    const string archivePath = "/tmp/w3_pipeline_output.cmp2";
    const string outputPath = "/tmp/w3_pipeline_restored.bin";
    LiteralBlockProducer producer;
    OutputSink sink;
    PipelineController pipeline(producer, sink);

    roundTrip(pipeline, {}, inputPath, archivePath, outputPath);
    roundTrip(pipeline, {0x41}, inputPath, archivePath, outputPath);
    roundTrip(pipeline, {0x41, 0x42}, inputPath, archivePath, outputPath);
    roundTrip(pipeline, vector<uint8_t>(32768, 'A'), inputPath, archivePath, outputPath);
    const cmp2::ContainerFile repetitive = cmp2::read_container(archivePath);
    assert(repetitive.blocks.front().type == cmp2::BlockType::Compressed);

    vector<uint8_t> exactBoundary(1024 * 1024);
    mt19937 generator(16);
    for (uint8_t& byte : exactBoundary) byte = static_cast<uint8_t>(generator());
    roundTrip(pipeline, exactBoundary, inputPath, archivePath, outputPath);

    vector<uint8_t> randomBytes(1024 * 1024 + 1);
    generator.seed(17);
    for (uint8_t& byte : randomBytes) byte = static_cast<uint8_t>(generator());
    roundTrip(pipeline, randomBytes, inputPath, archivePath, outputPath);

    const cmp2::ContainerFile storedFile = cmp2::read_container(archivePath);
    assert(!storedFile.blocks.empty());
    assert(storedFile.blocks.front().type == cmp2::BlockType::Stored);

    remove(inputPath.c_str());
    remove(archivePath.c_str());
    remove(outputPath.c_str());
}

void testCorruptPayloadRejectedBeforeDecode() {
    const string input = "/tmp/w3_pipeline_corrupt_input.bin";
    const string path = "/tmp/w3_pipeline_corrupt.cmp2";
    const string output = "/tmp/w3_pipeline_corrupt_out.bin";
    LiteralBlockProducer producer;
    OutputSink sink;
    PipelineController pipeline(producer, sink);
    const vector<uint8_t> source(8192, 0x5A);
    writeBytes(input, source);
    pipeline.compressFile(input, path);

    vector<uint8_t> bytes = readBytes(path);
    assert(bytes.size() > 20);
    size_t payloadOffset = 6;
    ++payloadOffset;
    readVarint(bytes, payloadOffset);
    readVarint(bytes, payloadOffset);
    readVarint(bytes, payloadOffset);
    assert(payloadOffset + 4 < bytes.size());
    bytes[payloadOffset] ^= 0x01;
    writeBytes(path, bytes);

    bool rejected = false;
    try {
        pipeline.decompressFile(path, output);
    } catch (const runtime_error& error) {
        rejected = string(error.what()).find("CRC32 mismatch") != string::npos;
    }
    assert(rejected);
    remove(input.c_str());
    remove(path.c_str());
    remove(output.c_str());
}

void testTruncatedContainerRejected() {
    const string path = "/tmp/w3_pipeline_truncated.cmp2";
    const string output = "/tmp/w3_pipeline_truncated_out.bin";
    cmp2::ContainerWriter writer = cmp2::ContainerWriter::open(path, 15);
    const vector<uint8_t> payload{0x10, 0x20, 0x30, 0x40};
    writer.writeBlock(cmp2::BlockType::Stored, true, payload.size(),
                      payload.size() * 8ULL, payload.data(), payload.size(),
                      w3::Crc32::compute(payload.data(), payload.size()));
    writer.finish();
    vector<uint8_t> bytes = readBytes(path);
    bytes.pop_back();
    writeBytes(path, bytes);

    LiteralBlockProducer producer;
    OutputSink sink;
    PipelineController pipeline(producer, sink);
    bool rejected = false;
    try {
        pipeline.decompressFile(path, output);
    } catch (const cmp2::ContainerError&) {
        rejected = true;
    }
    assert(rejected);
    remove(path.c_str());
    remove(output.c_str());
}

void testCliHelpAndArgumentValidation() {
    LiteralBlockProducer producer;
    OutputSink sink;
    PipelineController pipeline(producer, sink);
    ostringstream output;
    ostringstream errors;

    char* helpArguments[] = {
        const_cast<char*>("compressor"), const_cast<char*>("--help")};
    assert(CommandLineInterface::run(2, helpArguments, pipeline, output, errors) == 0);
    assert(output.str().find("Usage:") != string::npos);

    char* invalidArguments[] = {
        const_cast<char*>("compressor"), const_cast<char*>("-c"),
        const_cast<char*>("input"), const_cast<char*>("-o"),
        const_cast<char*>("output"), const_cast<char*>("-w"),
        const_cast<char*>("3000")};
    assert(CommandLineInterface::run(7, invalidArguments, pipeline, output, errors) == 1);
    assert(errors.str().find("power-of-two") != string::npos);
}

} // namespace

int main() {
    testRoundTripsAndSelection();
    testCorruptPayloadRejectedBeforeDecode();
    testTruncatedContainerRejected();
    testCliHelpAndArgumentValidation();
}