#pragma once

#include "lpc.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace slac {

// ──────────────────────────────────────────────────────────────
// Floor division by 2 (reversible stereo transform)
// ──────────────────────────────────────────────────────────────

inline int64_t floorDiv2(int64_t x) {
    return x / 2 - (x < 0 && (x % 2 != 0) ? 1 : 0);
}

// ──────────────────────────────────────────────────────────────
// Stereo decorrelation modes (§3.2)
// ──────────────────────────────────────────────────────────────

enum class StereoMode : uint8_t {
    LeftRight  = 0,  // L, R as-is
    MidSide    = 1,  // (L+R)/2, L-R
    LeftSide   = 2,  // L, L-R
    RightSide  = 3,  // R, L-R
};

// ──────────────────────────────────────────────────────────────
// Mid/Side (original API, kept for compat)
// ──────────────────────────────────────────────────────────────

inline void midSideEncode(
    const std::vector<int32_t>& left,
    const std::vector<int32_t>& right,
    std::vector<int32_t>& mid,
    std::vector<int32_t>& side
) {
    size_t n = std::min(left.size(), right.size());
    mid.resize(n);
    side.resize(n);

    for (size_t i = 0; i < n; ++i) {
        int64_t L = left[i];
        int64_t R = right[i];
        mid[i]  = static_cast<int32_t>(floorDiv2(L + R));
        side[i] = static_cast<int32_t>(L - R);
    }
}

inline void midSideDecode(
    const std::vector<int32_t>& mid,
    const std::vector<int32_t>& side,
    std::vector<int32_t>& left,
    std::vector<int32_t>& right
) {
    size_t n = std::min(mid.size(), side.size());
    left.resize(n);
    right.resize(n);

    for (size_t i = 0; i < n; ++i) {
        int64_t m = mid[i];
        int64_t s = side[i];
        left[i]  = static_cast<int32_t>(m + floorDiv2(s + 1));
        right[i] = static_cast<int32_t>(m - floorDiv2(s));
    }
}

// ──────────────────────────────────────────────────────────────
// Generic stereo transform (any mode → two streams)
// ──────────────────────────────────────────────────────────────

inline void stereoEncode(
    const std::vector<int32_t>& left,
    const std::vector<int32_t>& right,
    StereoMode mode,
    std::vector<int32_t>& stream0,
    std::vector<int32_t>& stream1
) {
    size_t n = std::min(left.size(), right.size());
    stream0.resize(n);
    stream1.resize(n);

    for (size_t i = 0; i < n; ++i) {
        int64_t L = left[i];
        int64_t R = right[i];

        switch (mode) {
            case StereoMode::LeftRight:
                stream0[i] = static_cast<int32_t>(L);
                stream1[i] = static_cast<int32_t>(R);
                break;
            case StereoMode::MidSide:
                stream0[i] = static_cast<int32_t>(floorDiv2(L + R));
                stream1[i] = static_cast<int32_t>(L - R);
                break;
            case StereoMode::LeftSide:
                stream0[i] = static_cast<int32_t>(L);
                stream1[i] = static_cast<int32_t>(L - R);
                break;
            case StereoMode::RightSide:
                stream0[i] = static_cast<int32_t>(R);
                stream1[i] = static_cast<int32_t>(L - R);
                break;
        }
    }
}

inline void stereoDecode(
    const std::vector<int32_t>& stream0,
    const std::vector<int32_t>& stream1,
    StereoMode mode,
    std::vector<int32_t>& left,
    std::vector<int32_t>& right
) {
    size_t n = std::min(stream0.size(), stream1.size());
    left.resize(n);
    right.resize(n);

    for (size_t i = 0; i < n; ++i) {
        int64_t s0 = stream0[i];
        int64_t s1 = stream1[i];

        switch (mode) {
            case StereoMode::LeftRight:
                left[i]  = static_cast<int32_t>(s0);
                right[i] = static_cast<int32_t>(s1);
                break;
            case StereoMode::MidSide:
                left[i]  = static_cast<int32_t>(s0 + floorDiv2(s1 + 1));
                right[i] = static_cast<int32_t>(s0 - floorDiv2(s1));
                break;
            case StereoMode::LeftSide:
                left[i]  = static_cast<int32_t>(s0);
                right[i] = static_cast<int32_t>(s0 - s1);
                break;
            case StereoMode::RightSide:
                right[i] = static_cast<int32_t>(s0);
                left[i]  = static_cast<int32_t>(s1 + s0);
                break;
        }
    }
}

// ──────────────────────────────────────────────────────────────
// Adaptive mode selection (§3.2)
//
// Estimates the bit cost of encoding a frame in each mode and
// returns the cheapest. Uses a quick heuristic based on the
// sum of absolute values (proxy for entropy).
// ──────────────────────────────────────────────────────────────

inline StereoMode selectBestStereoMode(
    const std::vector<int32_t>& left,
    const std::vector<int32_t>& right
) {
    size_t n = std::min(left.size(), right.size());
    if (n == 0) return StereoMode::MidSide;

    // Sum of absolute values per mode (proxy for entropy/cost).
    // Lower sum → lower entropy → better compression.
    uint64_t cost[4] = {0, 0, 0, 0};

    for (size_t i = 0; i < n; ++i) {
        int64_t L = left[i];
        int64_t R = right[i];

        int64_t mid  = floorDiv2(L + R);
        int64_t side = L - R;

        cost[0] += static_cast<uint64_t>(std::abs(L)) + static_cast<uint64_t>(std::abs(R));
        cost[1] += static_cast<uint64_t>(std::abs(mid)) + static_cast<uint64_t>(std::abs(side));
        cost[2] += static_cast<uint64_t>(std::abs(L)) + static_cast<uint64_t>(std::abs(side));
        cost[3] += static_cast<uint64_t>(std::abs(R)) + static_cast<uint64_t>(std::abs(side));
    }

    // Pick the lowest cost.
    StereoMode best = StereoMode::LeftRight;
    uint64_t bestCost = cost[0];

    for (int m = 1; m < 4; ++m) {
        if (cost[m] < bestCost) {
            bestCost = cost[m];
            best = static_cast<StereoMode>(m);
        }
    }

    return best;
}

} // namespace slac
