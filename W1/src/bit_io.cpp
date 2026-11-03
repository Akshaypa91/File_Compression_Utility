#include "bit_io.h"

using namespace std;

namespace w1 {

size_t BitWriter::finish() noexcept {
    if (accBits_ > 0) {
        // Pad the final partial byte with zero bits (the true length stays in bitCount()).
        const unsigned pad = (8u - (accBits_ & 7u)) & 7u;
        acc_ <<= pad;
        unsigned nb = accBits_ + pad;   // a multiple of 8, at most 32
        while (nb >= 8) {
            nb -= 8;
            if (pos_ < cap_) {
                buf_[pos_++] = static_cast<uint8_t>((acc_ >> nb) & 0xFFu);
            } else {
                overflow_ = true;
            }
        }
        accBits_ = 0;
    }
    return pos_;
}

}  // namespace w1