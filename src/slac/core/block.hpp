#pragma once

#include "bitstream.hpp"
#include "lpc.hpp"
#include "rice.hpp"
#include "range_coder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace slac {

namespace detail {

inline void writeSigned(BitWriter& bw, int32_t x, int bits) {
    if (bits < 0 || bits > 32)
        throw std::invalid_argument("writeSigned: bits must be in [0, 32]");
    if (bits == 0) return;
    if (bits == 32) {
        bw.writeBits(static_cast<uint32_t>(x), 32);
        return;
    }
    uint32_t mask = (1u << bits) - 1u;
    bw.writeBits(static_cast<uint32_t>(x) & mask, bits);
}

inline int32_t readSigned(BitReader& br, int bits) {
    if (bits < 0 || bits > 32)
        throw std::invalid_argument("readSigned: bits must be in [0, 32]");
    if (bits == 0) return 0;

    uint32_t v = br.readBits(bits);
    if (bits == 32) return static_cast<int32_t>(v);

    uint32_t signBit = 1u << (bits - 1);
    if (v & signBit) {
        uint32_t mask = ~((1u << bits) - 1u);
        v |= mask;
    }
    return static_cast<int32_t>(v);
}

inline int estimateRiceK(const int32_t* data, size_t count, int maxK = 15) {
    if (count == 0) return 0;
    maxK = std::min(maxK, 31);

    int bestK = 0;
    uint64_t bestCost = std::numeric_limits<uint64_t>::max();

    for (int k = 0; k <= maxK; ++k) {
        uint64_t cost = 0;
        for (size_t i = 0; i < count; ++i) {
            uint32_t u = foldSigned(data[i]);
            uint32_t q = u >> k;
            if (q >= kRiceEscapeZeroCount) {
                cost += kRiceEscapeZeroCount + 1 + 32;
            } else {
                cost += q + 1 + static_cast<uint64_t>(k);
            }
            if (cost >= bestCost) break;
        }
        if (cost < bestCost) {
            bestCost = cost;
            bestK = k;
        }
    }
    return bestK;
}

inline int estimateRiceK(const std::vector<int32_t>& v, int maxK = 15) {
    return estimateRiceK(v.data(), v.size(), maxK);
}

inline int detectWastedBits(const std::vector<int32_t>& samples) {
    if (samples.empty()) return 0;

    int minTrailing = 32;
    bool hasNonZero = false;

    for (int32_t s : samples) {
        if (s == 0) continue;
        hasNonZero = true;

        uint32_t u = static_cast<uint32_t>(s);
        int trailing = 0;
        while ((u & 1u) == 0u && trailing < 32) {
            ++trailing;
            u >>= 1;
        }
        minTrailing = std::min(minTrailing, trailing);
    }

    if (!hasNonZero) return 0;
    return (minTrailing >= 32) ? 0 : minTrailing;
}

inline int choosePartitionOrder(size_t residualCount) {
    if (residualCount == 0) return 0;

    int order = 0;
    for (int o = 4; o >= 0; --o) {
        size_t numPartitions = static_cast<size_t>(1) << o;
        if (residualCount / numPartitions >= 16) {
            order = o;
            break;
        }
    }
    return order;
}

} // namespace detail

// ──────────────────────────────────────────────────────────────
// Block format (v3 — entropy mode):
//
//   blockSize       u32
//   bitsPerSample   u8
//   wastedBits      u8
//   order           u8
//   shift           u8
//   coeffBits       u8
//   partitionOrder  u8
//   entropyMode     u8        (0 = Rice, 1 = Range)
//   [coefficients]  order × coeffBits
//   [warmup]        order × (bitsPerSample - wasted)
//   [residuals]     Rice partitions OU Range-coded
// ──────────────────────────────────────────────────────────────

inline std::vector<uint8_t> encodeBlockMono(
    const std::vector<int32_t>& samples,
    int bitsPerSample,
    int maxOrder = 8,
    int lpcShift = 15,
    bool useRangeCoding = false)
{
    if (bitsPerSample < 1 || bitsPerSample > 32)
        throw std::invalid_argument("encodeBlockMono: bitsPerSample [1,32]");
    if (lpcShift < 0 || lpcShift > 30)
        throw std::invalid_argument("encodeBlockMono: lpcShift [0,30]");

    BitWriter bw;

    uint32_t blockSize = static_cast<uint32_t>(samples.size());
    bw.writeBits(blockSize, 32);

    if (blockSize == 0) {
        bw.alignToByte();
        return bw.data();
    }

    // ── Wasted bits ─────────────────────────────────────────
    int wasted = detail::detectWastedBits(samples);

    std::vector<int32_t> shifted;
    const std::vector<int32_t>* input = &samples;

    if (wasted > 0) {
        shifted.resize(samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
            shifted[i] = samples[i] >> wasted;
        input = &shifted;
    }

    // ── LPC best-order + Tukey ──────────────────────────────
    int shift = lpcShift;
    int coeffBits = 16;
    int order = 0;

    QuantizedLpc q;
    std::vector<int32_t> residuals;

    if (maxOrder > 0 && input->size() > 1) {
        LpcBestResult bestLpc = analyzeLpcBestOrder(
            *input, maxOrder, shift, coeffBits, WindowType::Tukey, 0.5);

        order = bestLpc.order;
        q = bestLpc.lpc;
        residuals = std::move(bestLpc.residuals);
    } else {
        q.order = 0;
        q.shift = 0;
        residuals = *input;
    }

    // ── Rice partitioning (usado só no modo Rice) ───────────
    int partitionOrder = detail::choosePartitionOrder(residuals.size());
    int numPartitions = 1 << partitionOrder;

    while (numPartitions > 1 &&
           residuals.size() < static_cast<size_t>(numPartitions)) {
        --partitionOrder;
        numPartitions = 1 << partitionOrder;
    }

    std::vector<int> partitionK(numPartitions);
    size_t basePartSize = residuals.size() / numPartitions;
    size_t remainder = residuals.size() % numPartitions;

    if (!useRangeCoding) {
        size_t offset = 0;
        for (int p = 0; p < numPartitions; ++p) {
            size_t partSize = basePartSize + (static_cast<size_t>(p) < remainder ? 1 : 0);
            partitionK[p] = detail::estimateRiceK(residuals.data() + offset, partSize, 15);
            offset += partSize;
        }
    }

    // ── Header ──────────────────────────────────────────────
    bw.writeBits(static_cast<uint32_t>(bitsPerSample), 8);
    bw.writeBits(static_cast<uint32_t>(wasted), 8);
    bw.writeBits(static_cast<uint32_t>(order), 8);
    bw.writeBits(static_cast<uint32_t>(shift), 8);
    bw.writeBits(static_cast<uint32_t>(coeffBits), 8);
    bw.writeBits(static_cast<uint32_t>(partitionOrder), 8);
    bw.writeBits(useRangeCoding ? 1u : 0u, 8);

    // ── Coefficients + warmup ───────────────────────────────
    if (order > 0) {
        for (int i = 0; i < order; ++i)
            detail::writeSigned(bw, q.coeffs[i], coeffBits);
        for (int i = 0; i < order; ++i)
            detail::writeSigned(bw, (*input)[i], bitsPerSample - wasted);
    }

    // ── Residuals ───────────────────────────────────────────
    if (useRangeCoding) {
        // Range coding: modelo adaptativo sobre resíduos folded.
        RangeEncoder renc;
        AdaptiveByteModel model;

        for (int32_t res : residuals) {
            uint32_t u = foldSigned(res);
            model.encode(renc, u);
        }

        std::vector<uint8_t> rbytes = renc.finalize();

        bw.alignToByte();
        bw.writeBits(static_cast<uint32_t>(rbytes.size()), 32);
        for (uint8_t b : rbytes)
            bw.writeBits(b, 8);
    } else {
        // Rice particionado.
        size_t offset = 0;
        for (int p = 0; p < numPartitions; ++p) {
            size_t partSize = basePartSize + (static_cast<size_t>(p) < remainder ? 1 : 0);

            bw.writeBits(static_cast<uint32_t>(partitionK[p]), 8);

            for (size_t i = 0; i < partSize; ++i)
                RiceCoder::encode(bw, residuals[offset + i], partitionK[p]);

            offset += partSize;
        }
    }

    bw.alignToByte();
    return bw.data();
}

inline std::vector<int32_t> decodeBlockMono(
    const uint8_t* data,
    size_t size,
    size_t* consumedBytes = nullptr)
{
    if (consumedBytes) *consumedBytes = 0;
    if (data == nullptr || size == 0) return {};

    BitReader br(data, size);

    uint32_t blockSize = br.readBits(32);

    if (blockSize == 0) {
        br.alignToByte();
        if (consumedBytes) *consumedBytes = br.bytePosition();
        return {};
    }

    int bitsPerSample  = static_cast<int>(br.readBits(8));
    int wasted         = static_cast<int>(br.readBits(8));
    int order          = static_cast<int>(br.readBits(8));
    int shift          = static_cast<int>(br.readBits(8));
    int coeffBits      = static_cast<int>(br.readBits(8));
    int partitionOrder = static_cast<int>(br.readBits(8));
    int entropyMode    = static_cast<int>(br.readBits(8));

    if (bitsPerSample < 1 || bitsPerSample > 32)
        throw std::runtime_error("decodeBlockMono: invalid bitsPerSample");
    if (wasted < 0 || wasted > 31)
        throw std::runtime_error("decodeBlockMono: invalid wastedBits");
    if (order < 0 || order > 255)
        throw std::runtime_error("decodeBlockMono: invalid order");
    if (shift < 0 || shift > 30)
        throw std::runtime_error("decodeBlockMono: invalid shift");
    if (coeffBits < 0 || coeffBits > 32)
        throw std::runtime_error("decodeBlockMono: invalid coeffBits");
    if (partitionOrder < 0 || partitionOrder > 8)
        throw std::runtime_error("decodeBlockMono: invalid partitionOrder");
    if (entropyMode < 0 || entropyMode > 1)
        throw std::runtime_error("decodeBlockMono: invalid entropyMode");

    int numPartitions = 1 << partitionOrder;

    QuantizedLpc q;
    q.order = order;
    q.shift = shift;
    q.coeffs.resize(order);

    for (int i = 0; i < order; ++i)
        q.coeffs[i] = detail::readSigned(br, coeffBits);

    std::vector<int32_t> out(blockSize, 0);

    int warmupBits = bitsPerSample - wasted;
    for (size_t i = 0; i < static_cast<size_t>(order) && i < out.size(); ++i)
        out[i] = detail::readSigned(br, warmupBits);

    size_t idx = static_cast<size_t>(order);

    if (entropyMode == 1) {
        // ── Range coding ────────────────────────────────────
        br.alignToByte();
        uint32_t rsize = br.readBits(32);
        std::vector<uint8_t> rbytes(rsize);
        for (uint32_t i = 0; i < rsize; ++i)
            rbytes[i] = static_cast<uint8_t>(br.readBits(8));

        RangeDecoder rdec(rbytes.data(), rbytes.size());
        AdaptiveByteModel model;

        size_t residualCount = blockSize - static_cast<size_t>(order);
        for (size_t i = 0; i < residualCount; ++i) {
            uint32_t u = model.decode(rdec);
            int32_t residual = unfoldSigned(u);

            int32_t pred = predictSample(out, idx, q);
            int64_t value = static_cast<int64_t>(pred) + static_cast<int64_t>(residual);
            out[idx] = static_cast<int32_t>(value);
            ++idx;
        }
    } else {
        // ── Rice particionado ───────────────────────────────
        size_t residualCount = blockSize - static_cast<size_t>(order);
        size_t basePartSize = residualCount / numPartitions;
        size_t remainder = residualCount % numPartitions;

        for (int p = 0; p < numPartitions; ++p) {
            size_t partSize = basePartSize + (static_cast<size_t>(p) < remainder ? 1 : 0);

            int riceK = static_cast<int>(br.readBits(8));
            if (riceK < 0 || riceK > 31)
                throw std::runtime_error("decodeBlockMono: invalid riceK");

            for (size_t i = 0; i < partSize; ++i) {
                int32_t residual = RiceCoder::decode(br, riceK);
                int32_t pred = predictSample(out, idx, q);
                int64_t value = static_cast<int64_t>(pred) + static_cast<int64_t>(residual);
                out[idx] = static_cast<int32_t>(value);
                ++idx;
            }
        }
    }

    if (wasted > 0) {
        for (auto& s : out)
            s <<= wasted;
    }

    br.alignToByte();
    if (consumedBytes) *consumedBytes = br.bytePosition();

    return out;
}

inline std::vector<int32_t> decodeBlockMono(const std::vector<uint8_t>& bytes) {
    return decodeBlockMono(bytes.data(), bytes.size(), nullptr);
}

} // namespace slac
