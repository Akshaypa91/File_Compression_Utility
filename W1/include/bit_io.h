// bit_io.h - true bit-level I/O (MSB-first), written and read by W1 only.
//
// BitWriter writes straight into a buffer owned by W3 (no private vector, no extra copy).
// BitReader reads from such a buffer and refuses to read past the exact payloadBits, so the
// zero padding in the last byte can never be mistaken for data.
#pragma once

#include <cstddef>
#include <cstdint>

namespace w1 {

class BitWriter {
public:
    BitWriter(std::uint8_t* buffer, std::size_t capacity) noexcept
        : buf_(buffer), cap_(capacity) {}

    // Appends the low `nbits` bits of `value`, most significant bit first. nbits in [0, 32].
    void write(std::uint32_t value, unsigned nbits) noexcept {
        if (nbits == 0) return;
        const std::uint64_t mask = (std::uint64_t{1} << nbits) - 1;
        acc_ = (acc_ << nbits) | (static_cast<std::uint64_t>(value) & mask);
        accBits_ += nbits;
        totalBits_ += nbits;
        if (accBits_ >= 32) flush32();
    }

    // Number of meaningful bits written so far (this is the value stored as payloadBits).
    std::uint64_t bitCount() const noexcept { return totalBits_; }
    // True if the buffer was too small. Bytes after the overflow point are not written.
    bool overflow() const noexcept { return overflow_; }
    // Zero-pads the last byte and flushes. Returns the total number of bytes written.
    std::size_t finish() noexcept;

private:
    void flush32() noexcept {
        const std::uint32_t w = static_cast<std::uint32_t>(acc_ >> (accBits_ - 32));
        accBits_ -= 32;
        if (pos_ + 4 <= cap_) {
            buf_[pos_ + 0] = static_cast<std::uint8_t>(w >> 24);
            buf_[pos_ + 1] = static_cast<std::uint8_t>(w >> 16);
            buf_[pos_ + 2] = static_cast<std::uint8_t>(w >> 8);
            buf_[pos_ + 3] = static_cast<std::uint8_t>(w);
            pos_ += 4;
        } else {
            overflow_ = true;
        }
    }

    std::uint8_t* buf_;
    std::size_t cap_;
    std::size_t pos_ = 0;
    std::uint64_t acc_ = 0;        // right-aligned pending bits (stale high bits are harmless)
    unsigned accBits_ = 0;         // number of pending bits, always < 32 between calls
    std::uint64_t totalBits_ = 0;
    bool overflow_ = false;
};

class BitReader {
public:
    // `totalBits` is the exact number of meaningful bits (payloadBits from the container).
    BitReader(const std::uint8_t* data, std::size_t size, std::uint64_t totalBits) noexcept
        : data_(data), size_(size), totalBits_(totalBits) {
        const std::uint64_t capacity = static_cast<std::uint64_t>(size) * 8u;
        if (totalBits_ > capacity) {   // payloadBits larger than the payload: reject
            valid_ = false;
            totalBits_ = capacity;
        }
    }

    bool valid() const noexcept { return valid_; }
    std::uint64_t bitsLeft() const noexcept { return totalBits_ - consumed_; }

    // Tops the accumulator up to at least 56 bits (fewer only at the end of the data).
    void refill() noexcept {
        if (accBits_ > 56) return;
        if (pos_ + 8 <= size_) {
            std::uint64_t w = 0;
            for (unsigned i = 0; i < 8; ++i) w = (w << 8) | data_[pos_ + i];
            acc_ |= w >> accBits_;
            const int take = (63 - accBits_) >> 3;
            pos_ += static_cast<std::size_t>(take);
            accBits_ += take * 8;
        } else {
            while (accBits_ <= 56 && pos_ < size_) {
                acc_ |= static_cast<std::uint64_t>(data_[pos_++]) << (56 - accBits_);
                accBits_ += 8;
            }
        }
    }

    // Next n bits (n in [0, 32]) without consuming; bits past the data read as zero.
    std::uint32_t peek(unsigned n) const noexcept {
        return n == 0 ? 0u : static_cast<std::uint32_t>(acc_ >> (64 - n));
    }
    void consume(unsigned n) noexcept {
        acc_ <<= n;
        accBits_ -= static_cast<int>(n);
        consumed_ += n;
    }

    // Reads n bits (n in [0, 32]). Returns false, and consumes nothing, if fewer than n
    // meaningful bits remain.
    bool readBits(unsigned n, std::uint32_t& out) noexcept {
        if (n == 0) {
            out = 0;
            return true;
        }
        if (n > bitsLeft()) return false;
        if (accBits_ < static_cast<int>(n)) refill();
        out = peek(n);
        consume(n);
        return true;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::uint64_t totalBits_;
    std::size_t pos_ = 0;
    std::uint64_t acc_ = 0;        // left-aligned: the next bit is the top bit
    int accBits_ = 0;
    std::uint64_t consumed_ = 0;
    bool valid_ = true;
};

}  // namespace w1
