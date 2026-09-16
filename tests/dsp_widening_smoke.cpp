#include <slac/core/container.hpp>
#include <slac/dsp/processor.hpp>

#include <cmath>
#include <iostream>
#include <vector>

using namespace slac;

static bool checkEqual(
    const std::vector<int32_t>& expected,
    const std::vector<int32_t>& actual,
    const char* label
) {
    if (expected.size() != actual.size()) {
        std::cerr << "[" << label << "] size mismatch: expected "
                  << expected.size() << ", got " << actual.size() << "\n";
        return false;
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual[i]) {
            std::cerr << "[" << label << "] mismatch at index " << i
                      << ": expected " << expected[i]
                      << ", got " << actual[i] << "\n";
            return false;
        }
    }

    return true;
}

static dsp::SpatialParams toDspParams(const SpatMetadata& metadata) {
    dsp::SpatialParams params;

    params.wideness_permille = metadata.wideness_permille;
    params.reverb_wet_pct = metadata.reverb_wet_pct;
    params.preset_id = metadata.preset_id;

    params.has_automation = (metadata.flags & 0x01u) != 0u;
    params.mono_safe = (metadata.flags & 0x02u) != 0u;

    params.chain_order = metadata.chain_order;
    params.fallback_category = metadata.fallback_category;
    params.fallback_size = metadata.fallback_size;
    params.fallback_decay = metadata.fallback_decay;

    return params;
}

static std::pair<int32_t, int32_t> expectedWidenedSample(
    int32_t left,
    int32_t right,
    uint16_t wideness_permille,
    int bitsPerSample
) {
    int64_t L = left;
    int64_t R = right;

    int64_t mid = dsp::floorDiv2(L + R);
    int64_t side = L - R;

    int64_t scaledSide = dsp::mulDivRound(side, wideness_permille, 1000);

    int64_t widenedLeft = mid + dsp::floorDiv2(scaledSide + 1);
    int64_t widenedRight = mid - dsp::floorDiv2(scaledSide);

    return {
        dsp::clampToBits(widenedLeft, bitsPerSample),
        dsp::clampToBits(widenedRight, bitsPerSample)
    };
}

int main() {
    constexpr double pi = 3.14159265358979323846;
    constexpr int bits = 16;

    std::vector<int32_t> left;
    std::vector<int32_t> right;

    left.reserve(4096);
    right.reserve(4096);

    for (int i = 0; i < 4096; ++i) {
        double t = static_cast<double>(i) / 44100.0;

        double l =
            9000.0 * std::sin(2.0 * pi * 440.0 * t) +
            2000.0 * std::sin(2.0 * pi * 880.0 * t);

        double r =
            7000.0 * std::sin(2.0 * pi * 320.0 * t) +
            1800.0 * std::sin(2.0 * pi * 950.0 * t);

        left.push_back(static_cast<int32_t>(std::lround(l)));
        right.push_back(static_cast<int32_t>(std::lround(r)));
    }

    std::vector<std::vector<int32_t>> pcm;
    pcm.push_back(left);
    pcm.push_back(right);

    // --------------------------------------------------
    // 1. Wideness 100% must be exact bypass.
    // --------------------------------------------------
    dsp::SpatialParams params100;
    params100.wideness_permille = 1000;

    auto out100 = dsp::processOffline(pcm, bits, params100);

    if (!checkEqual(left, out100[0], "100% left")) {
        return 1;
    }

    if (!checkEqual(right, out100[1], "100% right")) {
        return 1;
    }

    std::cout << "[OK] Wideness 100% is exact bypass.\n";

    // --------------------------------------------------
    // 2. Wideness 0% collapses to mono.
    // --------------------------------------------------
    dsp::SpatialParams params0;
    params0.wideness_permille = 0;

    auto out0 = dsp::processOffline(pcm, bits, params0);

    if (!checkEqual(out0[0], out0[1], "0% mono L==R")) {
        return 1;
    }

    std::cout << "[OK] Wideness 0% collapses to mono.\n";

    // --------------------------------------------------
    // 3. Wideness 125% deterministic integer widening.
    // --------------------------------------------------
    dsp::SpatialParams params125;
    params125.wideness_permille = 1250;
    params125.mono_safe = true;

    auto out125 = dsp::processOffline(pcm, bits, params125);

    for (size_t i = 0; i < left.size(); ++i) {
        auto expected = expectedWidenedSample(left[i], right[i], 1250, bits);

        if (out125[0][i] != expected.first || out125[1][i] != expected.second) {
            std::cerr << "[FAIL] 125% mismatch at index " << i << "\n";
            std::cerr << "  input:    L=" << left[i] << ", R=" << right[i] << "\n";
            std::cerr << "  expected: L=" << expected.first << ", R=" << expected.second << "\n";
            std::cerr << "  actual:   L=" << out125[0][i] << ", R=" << out125[1][i] << "\n";
            return 1;
        }
    }

    std::cout << "[OK] Wideness 125% is deterministic.\n";

    // --------------------------------------------------
    // 4. Extreme widening must not exceed bit range.
    // --------------------------------------------------
    std::vector<int32_t> extremeLeft = {32767, 0, -32768, 10000};
    std::vector<int32_t> extremeRight = {-32768, 0, 32767, -10000};

    std::vector<std::vector<int32_t>> extremePcm = {extremeLeft, extremeRight};

    dsp::SpatialParams params150;
    params150.wideness_permille = 1500;

    auto out150 = dsp::processOffline(extremePcm, bits, params150);

    for (size_t i = 0; i < out150[0].size(); ++i) {
        if (out150[0][i] < -32768 || out150[0][i] > 32767 ||
            out150[1][i] < -32768 || out150[1][i] > 32767) {
            std::cerr << "[FAIL] 150% clipping test failed at index " << i << "\n";
            return 1;
        }
    }

    std::cout << "[OK] Wideness 150% clips safely within bit range.\n";

    // --------------------------------------------------
    // 5. Full .slac -> decode -> DSP pipeline.
    // --------------------------------------------------
    SpatMetadata metadata;
    metadata.wideness_permille = 1250;
    metadata.reverb_wet_pct = 0;
    metadata.preset_id = 0;
    metadata.flags = 0x02; // mono-safe

    SlacFormat fmt;
    fmt.sampleRate = 44100;
    fmt.bitsPerSample = bits;
    fmt.channelCount = 2;

    std::vector<uint8_t> file = encodeSlacFile(pcm, fmt, &metadata, 4, 15);

    SlacFormat decodedFmt;
    SpatMetadata decodedMetadata;

    auto decodedPcm = decodeSlacFile(file, &decodedFmt, &decodedMetadata);

    auto dspParams = toDspParams(decodedMetadata);
    auto processedPcm = dsp::processOffline(decodedPcm, decodedFmt.bitsPerSample, dspParams);

    if (!checkEqual(out125[0], processedPcm[0], "pipeline left")) {
        return 1;
    }

    if (!checkEqual(out125[1], processedPcm[1], "pipeline right")) {
        return 1;
    }

    std::cout << "[OK] Full .slac -> decode -> DSP pipeline matches direct DSP.\n";
    std::cout << "\n--- SLAC DSP widening MVP PASSED ---\n";

    return 0;
}
