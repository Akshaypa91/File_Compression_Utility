
#ifndef LZ77_BLOCK_PRODUCER_H
#define LZ77_BLOCK_PRODUCER_H

#include "pipeline_controller.hpp"

namespace cmp {
class LZ77BlockProducer : public worklet3::W2BlockProducer {
public:
    void encodeBlock(
        const std::uint8_t* rawBytes,
        std::size_t availableBytes,
        std::uint32_t windowSize,
        cmp::TokenBlock& block
    ) override;
};
}

#endif