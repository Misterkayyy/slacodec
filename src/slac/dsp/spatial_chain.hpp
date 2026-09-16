#pragma once

#include "convolver.hpp"
#include "fdn_reverb.hpp"
#include "limiter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace slac::dsp {

struct SpatialChainConfig {
    float wideness   = 1.0f;     // 1.0 = 100%
    bool  mono_safe  = true;
    const TrueStereoIR* hrir = nullptr;
    bool  hrir_unity_gain = true;   // normaliza HRIR p/ ganho coerente <= 1
    float hrir_echo_trim = 1.0f;    // 1.0 = off; <1 atenua taps apos o direto

    bool  reverb_enabled  = false;
    bool  reverb_adaptive = true;
    FdnReverbParams reverb;
    const TrueStereoIR* reverb_ir = nullptr;

    enum class LimitMode { Limit, Normalize, Loudness, None };
    LimitMode limit_mode = LimitMode::Normalize;
    float ceiling     = 0.988f;
    float lookahead_s = 0.008f;
    float release_s   = 0.120f;
};

struct SpatialChainStats {
    float peak_input          = 0.0f;
    float peak_input_db       = -200.0f;
    float rms_input           = 0.0f;
    float rms_input_db        = -200.0f;
    float peak_before_limit   = 0.0f;
    float peak_before_limit_db = -200.0f;
    float rms_before          = 0.0f;
    float rms_before_db       = -200.0f;
    float max_gain_reduction_db = 0.0f;
    float uniform_gain_db     = 0.0f;
    size_t out_samples = 0;
};

inline void widen_float(std::vector<float>& L, std::vector<float>& R, float w) {
    if (w == 1.0f) return;
    for (size_t i = 0; i < L.size(); ++i) {
        float mid  = 0.5f * (L[i] + R[i]);
        float side = L[i] - R[i];
        float s = side * w;
        L[i] = mid + 0.5f * s;
        R[i] = mid - 0.5f * s;
    }
}

inline float hrir_coherent_gain(const TrueStereoIR& ir) {
    auto sabs = [](const std::vector<float>& v) {
        double s = 0.0;
        for (float x : v) s += static_cast<double>(std::fabs(x));
        return s;
    };
    double gL = sabs(ir.ll) + sabs(ir.rl);
    double gR = sabs(ir.lr) + sabs(ir.rr);
    return static_cast<float>(std::max(gL, gR));
}

inline void conv_true_stereo_float(
    const std::vector<float>& inL, const std::vector<float>& inR,
    const TrueStereoIR& ir,
    std::vector<float>& outL, std::vector<float>& outR)
{
    size_t n = inL.size();
    size_t m = ir.length();
    if (m == 0) { outL = inL; outR = inR; return; }

    size_t olen = n + m - 1;
    outL.assign(olen, 0.0f);
    outR.assign(olen, 0.0f);

    auto acc = [&](const float* x, const float* h, std::vector<float>& y) {
        for (size_t i = 0; i < n; ++i) {
            float xi = x[i];
            if (xi == 0.0f) continue;
            for (size_t k = 0; k < m; ++k) y[i + k] += xi * h[k];
        }
    };

    acc(inL.data(), ir.ll.data(), outL);
    acc(inR.data(), ir.rl.data(), outL);
    acc(inL.data(), ir.lr.data(), outR);
    acc(inR.data(), ir.rr.data(), outR);
}

inline SpatialChainStats apply_spatial_chain(
    std::vector<int32_t>& pcmL,
    std::vector<int32_t>& pcmR,
    int bits,
    uint32_t sr,
    const SpatialChainConfig& cfg)
{
    SpatialChainStats st;
    size_t n = pcmL.size();
    if (n == 0) return st;

    const float scale = 1.0f / static_cast<float>(1 << (bits - 1));
    std::vector<float> L(n), R(n);
    double sumsq_in = 0.0;
    float peak_in = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        L[i] = static_cast<float>(pcmL[i]) * scale;
        R[i] = static_cast<float>(pcmR[i]) * scale;
        peak_in = std::max(peak_in, std::max(std::fabs(L[i]), std::fabs(R[i])));
        sumsq_in += static_cast<double>(L[i]) * L[i]
                  + static_cast<double>(R[i]) * R[i];
    }
    st.peak_input    = peak_in;
    st.peak_input_db = 20.0f * std::log10(peak_in + 1e-12f);
    st.rms_input     = static_cast<float>(std::sqrt(sumsq_in / (2.0 * n)));
    st.rms_input_db  = 20.0f * std::log10(st.rms_input + 1e-12f);

    // 1) widening
    widen_float(L, R, cfg.wideness);

    // 2) HRIR true-stereo (trim de ecos opcional + unity-gain)
    if (cfg.hrir && cfg.hrir->length() > 0) {
        TrueStereoIR h = *cfg.hrir;

        if (cfg.hrir_echo_trim < 0.999f) {
            auto trim_path = [&](std::vector<float>& v) {
                if (v.empty()) return;
                size_t d = 0;
                float mx = 0.0f;
                for (size_t i = 0; i < v.size(); ++i) {
                    float a = std::fabs(v[i]);
                    if (a > mx) { mx = a; d = i; }
                }
                for (size_t i = d + 1; i < v.size(); ++i) v[i] *= cfg.hrir_echo_trim;
            };
            trim_path(h.ll); trim_path(h.lr); trim_path(h.rl); trim_path(h.rr);
        }

        if (cfg.hrir_unity_gain) {
            float g = hrir_coherent_gain(h);
            if (g > 1.0f) {
                float k = 1.0f / g;
                for (auto& v : h.ll) v *= k;
                for (auto& v : h.lr) v *= k;
                for (auto& v : h.rl) v *= k;
                for (auto& v : h.rr) v *= k;
            }
        }

        std::vector<float> oL, oR;
        conv_true_stereo_float(L, R, h, oL, oR);
        L = std::move(oL);
        R = std::move(oR);
    }

    // 3) reverb: wet somado ao dry, sem clamp
    if (cfg.reverb_enabled && cfg.reverb.wet > 0.0f) {
        size_t m = L.size();
        std::vector<float> wL(m, 0.0f), wR(m, 0.0f);

        if (cfg.reverb_ir && cfg.reverb_ir->length() > 0) {
            std::vector<float> cL, cR;
            conv_true_stereo_float(L, R, *cfg.reverb_ir, cL, cR);
            cL.resize(m, 0.0f);
            cR.resize(m, 0.0f);
            for (size_t i = 0; i < m; ++i) {
                wL[i] = cL[i] * cfg.reverb.wet;
                wR[i] = cR[i] * cfg.reverb.wet;
            }
        } else {
            FdnReverb rev;
            rev.configure(cfg.reverb, sr);
            std::vector<float> wet_gain(m, cfg.reverb.wet);
            if (cfg.reverb_adaptive)
                compute_adaptive_wet_gain_float(L.data(), R.data(), m,
                                                cfg.reverb.wet, wet_gain.data());
            rev.process(L.data(), R.data(), wL.data(), wR.data(), m, wet_gain.data());
        }

        for (size_t i = 0; i < m; ++i) {
            L[i] += wL[i];
            R[i] += wR[i];
        }
    }

    // 4) medicao pos-cadeia + headroom
    double sumsq = 0.0;
    float peak = 0.0f;
    for (size_t i = 0; i < L.size(); ++i) {
        peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
        sumsq += static_cast<double>(L[i]) * L[i]
               + static_cast<double>(R[i]) * R[i];
    }
    st.peak_before_limit    = peak;
    st.peak_before_limit_db = 20.0f * std::log10(peak + 1e-12f);
    st.rms_before     = static_cast<float>(std::sqrt(sumsq / (2.0 * L.size())));
    st.rms_before_db  = 20.0f * std::log10(st.rms_before + 1e-12f);

    if (cfg.limit_mode == SpatialChainConfig::LimitMode::Normalize) {
        // peak align a fonte (teto ceiling): seguro, corpo = fonte - crest extra
        if (peak > 1e-9f) {
            float target = std::min(peak_in, cfg.ceiling);
            float g = target / peak;
            for (size_t i = 0; i < L.size(); ++i) { L[i] *= g; R[i] *= g; }
            st.uniform_gain_db = 20.0f * std::log10(g);
            st.max_gain_reduction_db = (g < 1.0f) ? -st.uniform_gain_db : 0.0f;
        }
    } else if (cfg.limit_mode == SpatialChainConfig::LimitMode::Loudness) {
        // RMS align a fonte: corpo igual ao original; o crest extra e pago
        // pelo limiter nos transientes que passarem do ceiling
        if (st.rms_before > 1e-9f) {
            float g = st.rms_input / st.rms_before;
            for (size_t i = 0; i < L.size(); ++i) { L[i] *= g; R[i] *= g; }
            st.uniform_gain_db = 20.0f * std::log10(g);
            float pk = peak * g;
            if (pk > cfg.ceiling)
                limit_stereo_float(L, R, sr, cfg.ceiling, cfg.lookahead_s,
                                   cfg.release_s, &st.max_gain_reduction_db);
        }
    } else if (cfg.limit_mode == SpatialChainConfig::LimitMode::Limit &&
               peak > cfg.ceiling) {
        limit_stereo_float(L, R, sr, cfg.ceiling, cfg.lookahead_s,
                           cfg.release_s, &st.max_gain_reduction_db);
    }
    // LimitMode::None: nada

    // 5) quantizacao final
    size_t outn = L.size();
    pcmL.resize(outn);
    pcmR.resize(outn);
    const float q = static_cast<float>((1 << (bits - 1)) - 1);
    for (size_t i = 0; i < outn; ++i) {
        float l = L[i] * q;
        float r = R[i] * q;
        if (l >  q) l =  q;
        if (l < -q) l = -q;
        if (r >  q) r =  q;
        if (r < -q) r = -q;
        pcmL[i] = static_cast<int32_t>(std::lround(l));
        pcmR[i] = static_cast<int32_t>(std::lround(r));
    }

    st.out_samples = outn;
    return st;
}

} // namespace slac::dsp
