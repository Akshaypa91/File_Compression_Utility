
#include "LZ77BlockProducer.h"
#include "LZ77.h"

#include <algorithm>
#include <stdexcept>

namespace cmp {

void LZ77BlockProducer::encodeBlock(
    const std::uint8_t* rawBytes,
    std::size_t availableBytes,
    std::uint32_t windowSize,
    cmp::TokenBlock& block
) {
    // Current W2 implementation uses a fixed 32 KB window.
    if (windowSize != LZ77_WINDOW_SIZE) {
        throw std::invalid_argument(
            "W2 currently supports only a 32768-byte window"
        );
    }

    // A non-empty input must have a valid source pointer.
    if (availableBytes > 0 && rawBytes == nullptr) {
        throw std::invalid_argument(
            "rawBytes cannot be null for non-empty input"
        );
    }

    // Each source byte can require at most one encoded word.
    // Limit this call so the TokenBlock cannot overflow.
    const std::size_t bytesToProcess =
        std::min(availableBytes,
                 static_cast<std::size_t>(kBlockTokens));

    const auto sourceLength =
        static_cast<std::uint32_t>(bytesToProcess);

    // Run the existing W2 compressor. It initializes and fills the block.
    if (!compressLZ77(block, rawBytes, sourceLength)) {
        throw std::runtime_error("W2 LZ77 compression failed");
    }

    // Tell W3 exactly how many source bytes this block represents.
    block.src = rawBytes;
    block.srcLen = sourceLength;
}

} // namespace cmp
// ls W2/include/LZ77BlockProducer.h W2/src/LZ77BlockProducer.cpp
// cd W2
// make test