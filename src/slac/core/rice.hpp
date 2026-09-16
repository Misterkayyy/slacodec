#pragma once

#include "bitstream.hpp"

#include <cstdint>
#include <stdexcept>

namespace slac {

static constexpr size_t kRiceEscapeZeroCount = 256;

inline uint32_t foldSigned(int32_t x) {
    if (x >= 0) {
        return static_cast<uint32_t>(x) << 1;
    }

    return (static_cast<uint32_t>(-(x + 1)) << 1) | 1u;
}

inline int32_t unfoldSigned(uint32_t u) {
    if ((u & 1u) == 0u) {
        return static_cast<int32_t>(u >> 1);
    }

    return -static_cast<int32_t>(u >> 1) - 1;
}

class RiceCoder {
public:
    static void encode(BitWriter& bw, int32_t value, int k) {
        if (k < 0 || k > 31) {
            throw std::invalid_argument("RiceCoder::encode: k must be in [0, 31]");
        }

        uint32_t u = foldSigned(value);
        uint32_t q = u >> k;

        if (q >= kRiceEscapeZeroCount) {
            bw.writeUnaryZerosThenOne(kRiceEscapeZeroCount);
            bw.writeBits(static_cast<uint32_t>(value), 32);
            return;
        }

        bw.writeUnaryZerosThenOne(q);

        if (k > 0) {
            uint32_t mask = (1u << k) - 1u;
            bw.writeBits(u & mask, k);
        }
    }

    static int32_t decode(BitReader& br, int k) {
        if (k < 0 || k > 31) {
            throw std::invalid_argument("RiceCoder::decode: k must be in [0, 31]");
        }

        size_t q = br.readUnaryZerosUntilOne(kRiceEscapeZeroCount);

        if (q == kRiceEscapeZeroCount) {
            uint32_t raw = br.readBits(32);
            return static_cast<int32_t>(raw);
        }

        uint32_t rem = 0;
        if (k > 0) {
            rem = br.readBits(k);
        }

        uint32_t u = (static_cast<uint32_t>(q) << k) | rem;
        return unfoldSigned(u);
    }
};

} // namespace slac
