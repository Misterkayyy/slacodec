#pragma once

#include "widener.hpp"

#include <cstdint>
#include <vector>

namespace slac::dsp {

struct SpatialParams {
    uint16_t wideness_permille = 1000;
    uint8_t reverb_wet_pct = 0;
    uint8_t preset_id = 0;

    bool has_automation = false;
    bool mono_safe = false;

    uint8_t chain_order = 0;

    uint8_t fallback_category = 0;
    uint8_t fallback_size = 0;
    uint8_t fallback_decay = 0;
};

inline std::vector<std::vector<int32_t>> processOffline(
    const std::vector<std::vector<int32_t>>& pcm,
    int bitsPerSample,
    const SpatialParams& params
) {
    if (pcm.empty()) {
        return {};
    }

    if (bitsPerSample < 1 || bitsPerSample > 32) {
        throw std::invalid_argument("processOffline: bitsPerSample must be in [1, 32]");
    }

    size_t frameCount = pcm[0].size();

    for (const auto& ch : pcm) {
        if (ch.size() != frameCount) {
            throw std::invalid_argument("processOffline: channel size mismatch");
        }
    }

    std::vector<std::vector<int32_t>> out = pcm;

    // Mono: no widening in this MVP.
    //
    // Future option:
    // - duplicate mono to stereo;
    // - apply safe artificial widening;
    // - or keep mono untouched.
    if (out.size() == 2) {
        WideningParams widening;
        widening.wideness_permille = params.wideness_permille;
        widening.mono_safe = params.mono_safe;

        processStereoWiden(
            pcm[0],
            pcm[1],
            out[0],
            out[1],
            bitsPerSample,
            widening
        );
    }

    // Future:
    //
    // if (params.reverb_wet_pct > 0 && params.preset_id != 0) {
    //     applyReverb(out, bitsPerSample, params);
    // }

    return out;
}

} // namespace slac::dsp
