#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace slac::dsp {

inline int64_t floorDiv2(int64_t x) {
    // Floor division by 2 for signed integers.
    //
    // Examples:
    // -3 -> -2
    // -2 -> -1
    // -1 -> -1
    //  0 ->  0
    //  1 ->  0
    //  2 ->  1
    //  3 ->  1
    return x / 2 - (x < 0 && (x % 2 != 0) ? 1 : 0);
}

inline int64_t mulDivRound(int64_t x, int64_t num, int64_t den) {
    if (den == 0) {
        throw std::invalid_argument("mulDivRound: denominator is zero");
    }

    if (x == 0 || num == 0) {
        return 0;
    }

    int64_t prod = x * num;
    int64_t half = den / 2;

    if (prod >= 0) {
        return (prod + half) / den;
    }

    return (prod - half) / den;
}

inline int32_t clampToBits(int64_t value, int bitsPerSample) {
    if (bitsPerSample < 1 || bitsPerSample > 32) {
        throw std::invalid_argument("clampToBits: bitsPerSample must be in [1, 32]");
    }

    int64_t minValue;
    int64_t maxValue;

    if (bitsPerSample == 32) {
        minValue = std::numeric_limits<int32_t>::min();
        maxValue = std::numeric_limits<int32_t>::max();
    } else {
        minValue = -(1LL << (bitsPerSample - 1));
        maxValue = (1LL << (bitsPerSample - 1)) - 1;
    }

    if (value < minValue) {
        value = minValue;
    }

    if (value > maxValue) {
        value = maxValue;
    }

    return static_cast<int32_t>(value);
}

struct WideningParams {
    // 1000 = 100.0%
    // 0    = mono collapse
    // 1500 = 150.0%
    uint16_t wideness_permille = 1000;

    // Reserved for future policies.
    // For now, the M/S gain algorithm is already mono-sum safe.
    bool mono_safe = true;
};

inline void processStereoWiden(
    const std::vector<int32_t>& inLeft,
    const std::vector<int32_t>& inRight,
    std::vector<int32_t>& outLeft,
    std::vector<int32_t>& outRight,
    int bitsPerSample,
    const WideningParams& params
) {
    if (inLeft.size() != inRight.size()) {
        throw std::invalid_argument("processStereoWiden: channel size mismatch");
    }

    if (bitsPerSample < 1 || bitsPerSample > 32) {
        throw std::invalid_argument("processStereoWiden: bitsPerSample must be in [1, 32]");
    }

    size_t n = inLeft.size();

    outLeft.resize(n);
    outRight.resize(n);

    uint16_t wideness = std::min<uint16_t>(params.wideness_permille, 1500);

    // Exact bypass.
    if (wideness == 1000) {
        outLeft = inLeft;
        outRight = inRight;
        return;
    }

    for (size_t i = 0; i < n; ++i) {
        int64_t L = inLeft[i];
        int64_t R = inRight[i];

        // Mid/side decomposition.
        //
        // mid is floor((L + R) / 2)
        // side is L - R
        int64_t mid = floorDiv2(L + R);
        int64_t side = L - R;

        // Apply stereo width.
        //
        // wideness = 0    -> mono collapse
        // wideness = 1000 -> original stereo
        // wideness = 1500 -> exaggerated stereo
        int64_t scaledSide = mulDivRound(side, wideness, 1000);

        // Reconstruct L/R using the same reversible-style formulas used by the codec.
        //
        // For scaledSide = original side, this reconstructs the original stereo pair.
        int64_t widenedLeft = mid + floorDiv2(scaledSide + 1);
        int64_t widenedRight = mid - floorDiv2(scaledSide);

        outLeft[i] = clampToBits(widenedLeft, bitsPerSample);
        outRight[i] = clampToBits(widenedRight, bitsPerSample);
    }
}

} // namespace slac::dsp
