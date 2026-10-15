#include "pipeline_controller.hpp"

#include "crc32.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace std;

namespace worklet3 {
namespace {

uint64_t getFileSize(ifstream& input) {
    input.seekg(0, ios::end);
    const streampos end = input.tellg();
    if (end < 0) throw runtime_error("Unable to determine input file size");
    input.seekg(0, ios::beg);
    return static_cast<uint64_t>(static_cast<streamoff>(end));
}

void reportProgress(ostream* progress, uint64_t completed, uint64_t& nextReport) {
    if (progress == nullptr) return;
    while (completed >= nextReport) {
        *progress << "Processed " << completed / (1024ULL * 1024ULL)
                  << " MiB\n";
        nextReport += kProgressIntervalBytes;
    }
}

[[noreturn]] void throwW1Failure(w1::Status status, const char* operation) {
    throw runtime_error(string("W1 ") + operation + " failed: " +
                        w1::statusName(status));
}

void validateTokenBlock(const cmp::TokenBlock& block,
                        const uint8_t* expectedSource,
                        size_t availableBytes) {
    if (block.src != expectedSource || block.srcLen == 0 ||
        block.srcLen > availableBytes ||
        block.srcLen > cmp2::kMaxSourceBytesPerBlock ||
        block.n == 0 || block.n > cmp::kBlockTokens) {
        throw runtime_error("W2 returned an invalid token block");
    }
}

} // namespace

PipelineController::PipelineController(W2BlockProducer& producer,
                                       W2DecodeSink& decoder)
    : producer_(producer), decoder_(decoder) {}

void PipelineController::compressFile(const string& inputPath,
                                      const string& outputPath,
                                      uint8_t log2Window,
                                      ostream* progress) {
    if (log2Window > cmp2::kMaxWindowLog2) {
        throw invalid_argument("LZ77 window exponent exceeds 15");
    }
    ifstream input(inputPath, ios::binary);
    if (!input) throw runtime_error("Unable to open input file: " + inputPath);
    const uint64_t fileSize = getFileSize(input);
    const uint32_t windowSize = 1U << log2Window;
    cmp2::ContainerWriter writer = cmp2::ContainerWriter::open(outputPath, log2Window);

    if (fileSize == 0) {
        const uint32_t crc = w3::Crc32::compute(nullptr, 0);
        writer.writeBlock(cmp2::BlockType::Stored, true, 0, 0, nullptr, 0, crc);
        writer.finish();
        return;
    }

    vector<uint8_t> sourceBuffer;
    vector<uint8_t> payloadBuffer;
    uint64_t bytesLoaded = 0;
    uint64_t bytesCovered = 0;
    uint64_t nextReport = kProgressIntervalBytes;
    size_t sourceBufferSize = 0;
    size_t sourceBufferOffset = 0;
    cmp::TokenBlock tokenBlock{};

    while (bytesCovered < fileSize) {
        if (sourceBufferOffset == sourceBufferSize) {
            const size_t request = static_cast<size_t>(min<uint64_t>(
                fileSize - bytesLoaded, cmp2::kMaxSourceBytesPerBlock));
            if (sourceBuffer.size() < request) sourceBuffer.resize(request);
            input.read(reinterpret_cast<char*>(sourceBuffer.data()),
                       static_cast<streamsize>(request));
            if (input.gcount() != static_cast<streamsize>(request)) {
                throw runtime_error("Short read while loading input block");
            }
            bytesLoaded += request;
            sourceBufferSize = request;
            sourceBufferOffset = 0;
        }

        const uint8_t* blockSource = sourceBuffer.data() + sourceBufferOffset;
        const size_t available = sourceBufferSize - sourceBufferOffset;
        tokenBlock.reset();
        producer_.encodeBlock(blockSource, available, windowSize, tokenBlock);
        validateTokenBlock(tokenBlock, blockSource, available);
        const bool isLast = tokenBlock.srcLen == fileSize - bytesCovered;

        const w1::Plan plan = w1::plan(tokenBlock);
        if (plan.status != w1::Status::Ok ||
            plan.totalBits() >= static_cast<uint64_t>(tokenBlock.srcLen) * 8ULL) {
            const uint32_t crc = w3::Crc32::compute(tokenBlock.src, tokenBlock.srcLen);
            writer.writeBlock(cmp2::BlockType::Stored, isLast, tokenBlock.srcLen,
                              static_cast<uint64_t>(tokenBlock.srcLen) * 8ULL,
                              tokenBlock.src, tokenBlock.srcLen, crc);
        } else {
            const size_t capacity = plan.totalBytes();
            if (payloadBuffer.size() < capacity) payloadBuffer.resize(capacity);
            w1::BitWriter bitWriter(payloadBuffer.data(), capacity);
            const w1::Status emitStatus = w1::emit(tokenBlock, plan, bitWriter);
            if (emitStatus != w1::Status::Ok) throwW1Failure(emitStatus, "emit");
            if (bitWriter.overflow()) {
                throw runtime_error("W1 emit exceeded its planned payload buffer");
            }
            const uint64_t payloadBits = bitWriter.bitCount();
            const size_t payloadBytes = bitWriter.finish();
            if (payloadBits != plan.totalBits() || payloadBytes > capacity) {
                throw runtime_error("W1 emit output did not match its plan");
            }
            const uint32_t crc = w3::Crc32::compute(payloadBuffer.data(), payloadBytes);
            writer.writeBlock(cmp2::BlockType::Compressed, isLast,
                              tokenBlock.srcLen, payloadBits, payloadBuffer.data(),
                              payloadBytes, crc);
        }

        sourceBufferOffset += tokenBlock.srcLen;
        bytesCovered += tokenBlock.srcLen;
        reportProgress(progress, bytesCovered, nextReport);
    }

    if (bytesCovered != fileSize || bytesLoaded != fileSize) {
        throw runtime_error("W2 token blocks did not cover the input file exactly");
    }
    writer.finish();
}

void PipelineController::decompressFile(const string& inputPath,
                                        const string& outputPath,
                                        ostream* progress) {
    cmp2::ContainerReader reader = cmp2::ContainerReader::open(inputPath);
    ofstream output(outputPath, ios::binary | ios::trunc);
    if (!output) throw runtime_error("Unable to open output file: " + outputPath);
    decoder_.setOutput(output);

    vector<uint8_t> payloadBuffer;
    w1::HuffTables tables{};
    uint64_t sourceBytes = 0;
    uint64_t nextReport = kProgressIntervalBytes;
    while (reader.hasNextBlock()) {
        const cmp2::ContainerBlock block = reader.readNextBlock(payloadBuffer);
        if (w3::Crc32::compute(payloadBuffer.data(), payloadBuffer.size()) != block.crc32) {
            throw runtime_error("CRC32 mismatch: refusing corrupted block before decoding");
        }

        const uint64_t before = decoder_.bytesProduced();
        if (block.type == cmp2::BlockType::Stored) {
            for (uint8_t byte : payloadBuffer) decoder_.lit(byte);
        } else {
            w1::BitReader bitReader(payloadBuffer.data(), payloadBuffer.size(),
                                    block.payloadBits);
            if (!bitReader.valid()) {
                throw runtime_error("Invalid W1 bit length in compressed block");
            }
            const w1::Status tableStatus = w1::readTables(bitReader, tables);
            if (tableStatus != w1::Status::Ok) throwW1Failure(tableStatus, "readTables");
            const w1::Status decodeStatus =
                w1::decodeBlock(bitReader, tables, decoder_);
            if (decodeStatus != w1::Status::Ok) throwW1Failure(decodeStatus, "decodeBlock");
        }

        const uint64_t produced = decoder_.bytesProduced() - before;
        if (produced != block.sourceLength) {
            throw runtime_error("Decoded block size does not match sourceLength");
        }
        if (produced > numeric_limits<uint64_t>::max() - sourceBytes) {
            throw runtime_error("Decoded output size overflows uint64_t");
        }
        sourceBytes += produced;
        reportProgress(progress, sourceBytes, nextReport);
    }

    reader.finish();
    output.flush();
    if (!output) throw runtime_error("Failed to flush decompressed output");
}

} // namespace worklet3