#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace slac {

class BitWriter {
public:
    void writeBit(bool bit) {
        if (bitPos_ == 0) {
            bytes_.push_back(0);
        }

        if (bit) {
            bytes_.back() |= static_cast<uint8_t>(1u << (7 - bitPos_));
        }

        if (++bitPos_ == 8) {
            bitPos_ = 0;
        }
    }

    void writeBits(uint32_t value, int n) {
        if (n < 0 || n > 32) {
            throw std::invalid_argument("BitWriter::writeBits: n must be in [0, 32]");
        }

        for (int i = n - 1; i >= 0; --i) {
            writeBit(((value >> i) & 1u) != 0u);
        }
    }

    void writeUnaryZerosThenOne(size_t zeros) {
        for (size_t i = 0; i < zeros; ++i) {
            writeBit(false);
        }
        writeBit(true);
    }

    const std::vector<uint8_t>& data() const {
        return bytes_;
    }

    void alignToByte() {
        bitPos_ = 0;
    }

private:
    std::vector<uint8_t> bytes_;
    int bitPos_ = 0;
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size)
        : data_(data), size_(size) {}

    bool readBit() {
        if (bytePos_ >= size_) {
            throw std::out_of_range("BitReader::readBit: end of stream");
        }

        uint8_t byte = data_[bytePos_];
        bool bit = (byte & (1u << (7 - bitPos_))) != 0u;

        if (++bitPos_ == 8) {
            bitPos_ = 0;
            ++bytePos_;
        }

        return bit;
    }

    uint32_t readBits(int n) {
        if (n < 0 || n > 32) {
            throw std::invalid_argument("BitReader::readBits: n must be in [0, 32]");
        }

        uint32_t value = 0;
        for (int i = 0; i < n; ++i) {
            value = (value << 1) | (readBit() ? 1u : 0u);
        }
        return value;
    }

    size_t readUnaryZerosUntilOne(size_t maxZeros) {
        size_t zeros = 0;

        while (true) {
            bool bit = readBit();
            if (bit) {
                return zeros;
            }

            ++zeros;
            if (zeros > maxZeros) {
                throw std::runtime_error("BitReader::readUnaryZerosUntilOne: unary code too long");
            }
        }
    }

    void alignToByte() {
        if (bitPos_ != 0) {
            bitPos_ = 0;
            if (bytePos_ < size_) {
                ++bytePos_;
            }
        }
    }

    size_t bytePosition() const {
        return bytePos_;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t bytePos_ = 0;
    int bitPos_ = 0;
};

} // namespace slac
