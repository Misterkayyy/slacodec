#pragma once

#include "slac/dsp/convolver.hpp"
#include "slac/dsp/fdn_reverb.hpp"
#include "slac/dsp/limiter.hpp"
#include "slac/dsp/partitioned_convolver.hpp"
#include "slac/dsp/spatial_chain.hpp"
#include "slac/core/auto_chunk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace slac::rt {

// ──────────────────────────────────────────────────────────────
// Processa a spatial chain em blocos pequenos (tempo real).
//
// Mantém estado entre blocos:
// - PartitionedConvolver (HRIR delay lines)
// - FdnReverb (FDN state)
// - Limiter (lookahead buffer, envelope)
//
// Não aplica normalização global (não conhece o áudio futuro).
// Usa LimitMode::Limit com ceiling se houver clipping.
// ──────────────────────────────────────────────────────────────
class BlockProcessor {
public:
    BlockProcessor() = default;

    void configure(const dsp::SpatialChainConfig& cfg, uint32_t sample_rate, size_t block_size) {
        cfg_ = cfg;
        sample_rate_ = sample_rate;
        block_size_ = block_size;
        global_sample_idx_ = 0;

        // Configura HRIR (partitioned convolver).
        if (cfg.hrir && cfg.hrir->length() > 0) {
            dsp::TrueStereoIR h = *cfg.hrir;
            if (cfg.hrir_unity_gain) {
                float g = dsp::hrir_coherent_gain(h);
                if (g > 1.0f) {
                    float k = 1.0f / g;
                    for (auto& v : h.ll) v *= k;
                    for (auto& v : h.lr) v *= k;
                    for (auto& v : h.rl) v *= k;
                    for (auto& v : h.rr) v *= k;
                }
            }
            hrir_conv_.configure(h, block_size);
            use_hrir_ = true;
        }

        // Configura reverb FDN.
        if (cfg.reverb_enabled && cfg.reverb.wet > 0.0f) {
            reverb_.configure(cfg.reverb, sample_rate);
            use_reverb_ = true;
        }

        // Configura automação.
        if (cfg.use_auto && !cfg.auto_keyframes.empty()) {
            std::vector<slac::core::AutoKeyframe> wideness_kfs;
            for (const auto& kf : cfg.auto_keyframes) {
                if (kf.param_id == static_cast<uint8_t>(slac::core::AutoParam::WidenessPermille)) {
                    slac::core::AutoKeyframe scaled = kf;
                    scaled.value = kf.value / 1000.0f;
                    wideness_kfs.push_back(scaled);
                }
            }
            wideness_curve_.emplace(
                wideness_kfs,
                static_cast<uint8_t>(slac::core::AutoParam::WidenessPermille),
                cfg.wideness,
                cfg.wideness_slew_rate,
                sample_rate
            );
            use_auto_wideness_ = true;

            if (cfg.reverb_enabled) {
                std::vector<slac::core::AutoKeyframe> reverb_kfs;
                for (const auto& kf : cfg.auto_keyframes) {
                    if (kf.param_id == static_cast<uint8_t>(slac::core::AutoParam::ReverbWetPct)) {
                        slac::core::AutoKeyframe scaled = kf;
                        scaled.value = kf.value / 100.0f;
                        reverb_kfs.push_back(scaled);
                    }
                }
                reverb_curve_.emplace(
                    reverb_kfs,
                    static_cast<uint8_t>(slac::core::AutoParam::ReverbWetPct),
                    cfg.reverb.wet,
                    cfg.reverb_wet_slew_rate,
                    sample_rate
                );
                use_auto_reverb_ = true;
            }
        }

        // Buffers temporários.
        tmp_L_.resize(block_size);
        tmp_R_.resize(block_size);
        wet_L_.resize(block_size);
        wet_R_.resize(block_size);
    }

    void process_block(const float* inL, const float* inR, float* outL, float* outR) {
        // 1. Widening (sample-a-sample com automação).
        if (use_auto_wideness_ && wideness_curve_.has_value()) {
            for (size_t i = 0; i < block_size_; ++i) {
                float w = wideness_curve_->get_sample(global_sample_idx_ + i);
                float mid = 0.5f * (inL[i] + inR[i]);
                float side = inL[i] - inR[i];
                float s = side * w;
                tmp_L_[i] = mid + 0.5f * s;
                tmp_R_[i] = mid - 0.5f * s;
            }
        } else {
            float w = cfg_.wideness;
            for (size_t i = 0; i < block_size_; ++i) {
                float mid = 0.5f * (inL[i] + inR[i]);
                float side = inL[i] - inR[i];
                float s = side * w;
                tmp_L_[i] = mid + 0.5f * s;
                tmp_R_[i] = mid - 0.5f * s;
            }
        }

        // 2. HRIR (partitioned convolver).
        if (use_hrir_) {
            hrir_conv_.process(tmp_L_.data(), tmp_R_.data(), outL, outR);
        } else {
            std::copy(tmp_L_.begin(), tmp_L_.end(), outL);
            std::copy(tmp_R_.begin(), tmp_R_.end(), outR);
        }

        // 3. Reverb (FDN, sample-a-sample).
        if (use_reverb_) {
            std::fill(wet_L_.begin(), wet_L_.end(), 0.0f);
            std::fill(wet_R_.begin(), wet_R_.end(), 0.0f);

            if (use_auto_reverb_ && reverb_curve_.has_value()) {
                std::vector<float> wet_gain(block_size_);
                for (size_t i = 0; i < block_size_; ++i) {
                    wet_gain[i] = reverb_curve_->get_sample(global_sample_idx_ + i);
                }
                reverb_.process(outL, outR, wet_L_.data(), wet_R_.data(), block_size_, wet_gain.data());
            } else {
                std::vector<float> wet_gain(block_size_, cfg_.reverb.wet);
                reverb_.process(outL, outR, wet_L_.data(), wet_R_.data(), block_size_, wet_gain.data());
            }

            for (size_t i = 0; i < block_size_; ++i) {
                outL[i] += wet_L_[i];
                outR[i] += wet_R_[i];
            }
        }

        // 4. Limiter (clip em ceiling).
        if (cfg_.limit_mode == dsp::SpatialChainConfig::LimitMode::Limit) {
            for (size_t i = 0; i < block_size_; ++i) {
                if (outL[i] > cfg_.ceiling) outL[i] = cfg_.ceiling;
                if (outL[i] < -cfg_.ceiling) outL[i] = -cfg_.ceiling;
                if (outR[i] > cfg_.ceiling) outR[i] = cfg_.ceiling;
                if (outR[i] < -cfg_.ceiling) outR[i] = -cfg_.ceiling;
            }
        }

        global_sample_idx_ += block_size_;
    }

    void reset() {
        global_sample_idx_ = 0;
        if (use_hrir_) hrir_conv_.reset();
        // FDN reverb mantém estado automaticamente.
    }

    size_t block_size() const { return block_size_; }
    uint64_t position() const { return global_sample_idx_; }

private:
    dsp::SpatialChainConfig cfg_;
    uint32_t sample_rate_ = 44100;
    size_t block_size_ = 128;
    uint64_t global_sample_idx_ = 0;

    bool use_hrir_ = false;
    dsp::TrueStereoPartitionedConvolver hrir_conv_;

    bool use_reverb_ = false;
    dsp::FdnReverb reverb_;

    bool use_auto_wideness_ = false;
    std::optional<dsp::AutomationCurve> wideness_curve_;

    bool use_auto_reverb_ = false;
    std::optional<dsp::AutomationCurve> reverb_curve_;

    std::vector<float> tmp_L_, tmp_R_;
    std::vector<float> wet_L_, wet_R_;
};

} // namespace slac::rt
