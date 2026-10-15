#pragma once

#include "bit_io.h"
#include "container_format.hpp"
#include "huffman.h"
#include "symbols.hpp"
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

using namespace std;

namespace worklet3 {
constexpr uint8_t kDefaultWindowLog2 = 15;
constexpr uint64_t kProgressIntervalBytes = 256ULL * 1024ULL * 1024ULL;

class W2BlockProducer {
public:
    virtual ~W2BlockProducer() = default;

    virtual void encodeBlock(const uint8_t* rawBytes,
                             size_t availableBytes,
                             uint32_t windowSize,
                             cmp::TokenBlock& block) = 0;
};

class W2DecodeSink {
public:
    virtual ~W2DecodeSink() = default;

    virtual void setOutput(ostream& output) = 0;
    virtual void lit(uint8_t byte) = 0;
    virtual bool match(uint32_t length, uint32_t distance) = 0;
    virtual uint64_t bytesProduced() const = 0;
};

class PipelineController {
public:
    explicit PipelineController(W2BlockProducer& producer,
                                W2DecodeSink& decoder);

    void compressFile(const string& inputPath,
                      const string& outputPath,
                      uint8_t log2Window = kDefaultWindowLog2,
                      ostream* progress = nullptr);
    void decompressFile(const string& inputPath,
                        const string& outputPath,
                        ostream* progress = nullptr);

private:
    W2BlockProducer& producer_;
    W2DecodeSink& decoder_;
};

} // namespace worklet3