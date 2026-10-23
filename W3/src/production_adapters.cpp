#include "production_adapters.hpp"

#include "LZ77.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace worklet3 {

void LZ77BlockProducer::encodeBlock(const uint8_t* rawBytes,
                                    size_t availableBytes,
                                    uint32_t windowSize,
                                    cmp::TokenBlock& block) {
    if (availableBytes != 0 && rawBytes == nullptr) {
        throw invalid_argument("W2 input block is null");
    }
    if (windowSize == 0 || windowSize > cmp::kMaxDistance) {
        throw invalid_argument("LZ77 window size is outside the supported range");
    }

    const size_t blockSize = min({availableBytes,
                                  static_cast<size_t>(cmp::kBlockTokens),
                                  static_cast<size_t>(numeric_limits<uint32_t>::max())});
    if (!cmp::compressLZ77(block, rawBytes, static_cast<uint32_t>(blockSize),
                           windowSize)) {
        throw runtime_error("W2 LZ77 compression failed");
    }
}

void OutputHistorySink::setOutput(ostream& output) {
    output_ = &output;
    produced_ = 0;
    beginBlock();
}

void OutputHistorySink::setWindowSize(uint32_t windowSize) {
    if (windowSize == 0 || windowSize > cmp::kMaxDistance) {
        throw invalid_argument("LZ77 window size is outside the supported range");
    }
    windowSize_ = windowSize;
}

void OutputHistorySink::beginBlock() {
    historySize_ = 0;
    historyPosition_ = 0;
}

void OutputHistorySink::lit(uint8_t byte) {
    if (output_ == nullptr) {
        throw runtime_error("Output stream is not set");
    }
    output_->put(static_cast<char>(byte));
    if (!*output_) {
        throw runtime_error("Output write failed");
    }
    remember(byte);
    ++produced_;
}

bool OutputHistorySink::match(uint32_t length, uint32_t distance) {
    if (distance == 0 || distance > windowSize_ || distance > historySize_) {
        return false;
    }

    for (uint32_t index = 0; index < length; ++index) {
        const size_t source = historySize_ < cmp::kMaxDistance
            ? historySize_ - distance
            : (historyPosition_ + cmp::kMaxDistance - distance) % cmp::kMaxDistance;
        lit(history_[source]);
    }
    return true;
}

void OutputHistorySink::remember(uint8_t byte) {
    if (historySize_ < cmp::kMaxDistance) {
        history_[historySize_++] = byte;
        if (historySize_ == cmp::kMaxDistance) {
            historyPosition_ = 0;
        }
        return;
    }
    history_[historyPosition_] = byte;
    historyPosition_ = (historyPosition_ + 1) % cmp::kMaxDistance;
}

} // namespace worklet3
