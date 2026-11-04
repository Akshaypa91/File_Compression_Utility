#include "../include/LZ77DecodeSink.h"

namespace cmp {

void LZ77DecodeSink::setOutput(std::ostream& output) {
    output_ = &output;

    historySize_ = 0;
    nextWrite_ = 0;
    produced_ = 0;
}

void LZ77DecodeSink::remember(std::uint8_t byte) {
    history_[nextWrite_] = byte;

    nextWrite_ = (nextWrite_ + 1) % kMaxDistance;

    if (historySize_ < kMaxDistance) {
        ++historySize_;
    }
}

void LZ77DecodeSink::lit(std::uint8_t byte) {
    if (output_ == nullptr) {
        throw std::runtime_error("Output stream is not set");
    }

    output_->put(static_cast<char>(byte));

    if (!(*output_)) {
        throw std::runtime_error("Output write failed");
    }

    remember(byte);
    ++produced_;
}

bool LZ77DecodeSink::match(
    std::uint32_t length,
    std::uint32_t distance
) {
    if (distance == 0 ||
        distance > kMaxDistance ||
        distance > historySize_ ||
        length < kMinMatch ||
        length > kMaxMatch) {
        return false;
    }

    if (output_ == nullptr) {
        return false;
    }

    for (std::uint32_t i = 0; i < length; ++i) {

        // Locate the byte at the requested backward distance.
        const std::size_t source =
            (nextWrite_ + kMaxDistance - distance) % kMaxDistance;

        const std::uint8_t byte = history_[source];

        // Writing the byte also updates the history.
        // This correctly handles overlapping matches.
        lit(byte);
    }

    return true;
}

std::uint64_t LZ77DecodeSink::bytesProduced() const {
    return produced_;
}

} // namespace cmp