#ifndef LZ77_DECODE_SINK_H
#define LZ77_DECODE_SINK_H

#include "pipeline_controller.hpp"
#include "symbols.hpp"

#include <array>
#include <cstdint>
#include <ostream>
#include <stdexcept>

namespace cmp {

class LZ77DecodeSink final : public worklet3::W2DecodeSink {
public:
    void setOutput(std::ostream& output) override;

    void lit(std::uint8_t byte) override;

    bool match(
        std::uint32_t length,
        std::uint32_t distance
    ) override;

    std::uint64_t bytesProduced() const override;

private:
    void remember(std::uint8_t byte);

    std::ostream* output_ = nullptr;

    // Circular history buffer.
    std::array<std::uint8_t, kMaxDistance> history_{};

    std::size_t historySize_ = 0;
    std::size_t nextWrite_ = 0;

    std::uint64_t produced_ = 0;
};

} // namespace cmp

#endif