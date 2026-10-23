#pragma once

#include "pipeline_controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

namespace worklet3 {

class LZ77BlockProducer final : public W2BlockProducer {
public:
    void encodeBlock(const uint8_t* rawBytes,
                     size_t availableBytes,
                     uint32_t windowSize,
                     cmp::TokenBlock& block) override;
};

class OutputHistorySink final : public W2DecodeSink {
public:
    void setOutput(ostream& output) override;
    void setWindowSize(uint32_t windowSize) override;
    void beginBlock() override;
    void lit(uint8_t byte) override;
    bool match(uint32_t length, uint32_t distance) override;
    uint64_t bytesProduced() const override { return produced_; }

private:
    void remember(uint8_t byte);

    ostream* output_ = nullptr;
    std::array<uint8_t, cmp::kMaxDistance> history_{};
    size_t historySize_ = 0;
    size_t historyPosition_ = 0;
    uint32_t windowSize_ = cmp::kMaxDistance;
    uint64_t produced_ = 0;
};

} // namespace worklet3
