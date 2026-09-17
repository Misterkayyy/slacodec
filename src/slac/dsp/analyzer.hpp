#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace slac::dsp {

// ──────────────────────────────────────────────────────────────
// Simple iterative radix-2 FFT (in-place, real+imag arrays)
// ──────────────────────────────────────────────────────────────

inline void fft_inplace(std::vector<float>& re, std::vector<float>& im) {
    size_t N = re.size();
    if (N <= 1) return;

    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < N; ++i) {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    // Butterfly.
    for (size_t len = 2; len <= N; len <<= 1) {
        float ang = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        float wlen_re = std::cos(ang);
        float wlen_im = std::sin(ang);

        for (size_t i = 0; i < N; i += len) {
            float w_re = 1.0f, w_im = 0.0f;
            size_t half = len >> 1;

            for (size_t j = 0; j < half; ++j) {
                float u_re = re[i + j];
                float u_im = im[i + j];

                float v_re = re[i + j + half] * w_re - im[i + j + half] * w_im;
                float v_im = re[i + j + half] * w_im + im[i + j + half] * w_re;

                re[i + j] = u_re + v_re;
                im[i + j] = u_im + v_im;
                re[i + j + half] = u_re - v_re;
                im[i + j + half] = u_im - v_im;

                float new_w_re = w_re * wlen_re - w_im * wlen_im;
                float new_w_im = w_re * wlen_im + w_im * wlen_re;
                w_re = new_w_re;
                w_im = new_w_im;
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────
// Audio features extracted from PCM
// ──────────────────────────────────────────────────────────────

struct AudioFeatures {
    // Stereo field.
    float mid_energy = 0.0f;
    float side_energy = 0.0f;
    float stereo_width = 0.0f;      // 0 = mono, 1 = fully wide
    float stereo_correlation = 0.0f; // 1 = identical L/R, 0 = uncorrelated

    // Spectral.
    float spectral_centroid = 0.0f;  // Hz, averaged across windows
    float spectral_flux = 0.0f;      // average change in centroid between windows

    // Energy.
    float rms_energy = 0.0f;

    // Derived spatial parameters.
    uint16_t suggested_wideness_permille = 1000;
    uint8_t  suggested_reverb_wet_pct = 0;
    uint8_t  suggested_preset_id = 0;
    uint8_t  suggested_fb_category = 0;
    uint8_t  suggested_fb_size = 0;
    uint8_t  suggested_fb_decay = 0;
};

// ──────────────────────────────────────────────────────────────
// Feature extraction
// ──────────────────────────────────────────────────────────────

inline AudioFeatures analyze_audio(
    const std::vector<int32_t>& left,
    const std::vector<int32_t>& right,
    int bits_per_sample,
    uint32_t sample_rate
) {
    AudioFeatures feat;
    size_t N = left.size();
    if (N == 0 || N != right.size()) return feat;

    float norm = 1.0f / static_cast<float>(1 << (bits_per_sample - 1));

    // ── 1. Mid/Side energy & stereo width ───────────────────
    double mid_e = 0.0, side_e = 0.0;
    double lr_corr_num = 0.0, l_energy = 0.0, r_energy = 0.0;
    double rms_sum = 0.0;

    for (size_t i = 0; i < N; ++i) {
        float L = static_cast<float>(left[i]) * norm;
        float R = static_cast<float>(right[i]) * norm;

        float mid = (L + R) * 0.5f;
        float side = (L - R) * 0.5f;

        mid_e += static_cast<double>(mid * mid);
        side_e += static_cast<double>(side * side);

        lr_corr_num += static_cast<double>(L * R);
        l_energy += static_cast<double>(L * L);
        r_energy += static_cast<double>(R * R);

        rms_sum += static_cast<double>(L * L + R * R);
    }

    feat.mid_energy = static_cast<float>(mid_e);
    feat.side_energy = static_cast<float>(side_e);

    float total_energy = feat.mid_energy + feat.side_energy;
    feat.stereo_width = (total_energy > 1e-10f)
        ? feat.side_energy / total_energy
        : 0.0f;

    double lr_denom = std::sqrt(l_energy * r_energy);
    feat.stereo_correlation = (lr_denom > 1e-10)
        ? static_cast<float>(lr_corr_num / lr_denom)
        : 1.0f;

    feat.rms_energy = static_cast<float>(std::sqrt(rms_sum / (2.0 * N)));

    // ── 2. Spectral analysis via FFT ────────────────────────
    //
    // We analyze a subset of windows for efficiency.
    // Window size: 4096, hop: 2048, sample every 10th window.
    const size_t WINDOW = 4096;
    const size_t HOP = 2048;
    const int SKIP = 10; // analyze every 10th window

    if (N < WINDOW) {
        // Track too short for spectral analysis — use defaults.
        feat.spectral_centroid = 2000.0f;
        feat.spectral_flux = 0.0f;
    } else {
        // Precompute Hann window.
        std::vector<float> hann(WINDOW);
        for (size_t i = 0; i < WINDOW; ++i) {
            hann[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (WINDOW - 1)));
        }

        std::vector<float> prev_magnitude(WINDOW / 2, 0.0f);
        double centroid_sum = 0.0;
        double flux_sum = 0.0;
        int window_count = 0;
        int flux_count = 0;

        size_t max_windows = (N - WINDOW) / HOP;
        size_t step = std::max<size_t>(1, max_windows / 200); // cap at ~200 analysis windows
        if (step < static_cast<size_t>(SKIP)) step = SKIP;

        for (size_t offset = 0; offset + WINDOW <= N; offset += HOP * step) {
            // Apply window.
            std::vector<float> re(WINDOW, 0.0f);
            std::vector<float> im(WINDOW, 0.0f);

            for (size_t i = 0; i < WINDOW; ++i) {
                float mono = (static_cast<float>(left[offset + i]) +
                              static_cast<float>(right[offset + i])) * 0.5f * norm;
                re[i] = mono * hann[i];
            }

            // FFT.
            fft_inplace(re, im);

            // Compute magnitude spectrum (first half).
            size_t half = WINDOW / 2;
            std::vector<float> magnitude(half);
            for (size_t k = 0; k < half; ++k) {
                magnitude[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]);
            }

            // Spectral centroid.
            double weighted_sum = 0.0, mag_sum = 0.0;
            float freq_resolution = static_cast<float>(sample_rate) / static_cast<float>(WINDOW);

            for (size_t k = 0; k < half; ++k) {
                float freq = static_cast<float>(k) * freq_resolution;
                weighted_sum += static_cast<double>(freq) * magnitude[k];
                mag_sum += magnitude[k];
            }

            float centroid = (mag_sum > 1e-10f)
                ? static_cast<float>(weighted_sum / mag_sum)
                : 0.0f;

            centroid_sum += centroid;
            ++window_count;

            // Spectral flux (change from previous window).
            if (window_count > 1) {
                double diff_sum = 0.0;
                for (size_t k = 0; k < half; ++k) {
                    float diff = magnitude[k] - prev_magnitude[k];
                    diff_sum += std::abs(diff);
                }
                flux_sum += diff_sum;
                ++flux_count;
            }

            prev_magnitude = magnitude;
        }

        feat.spectral_centroid = (window_count > 0)
            ? static_cast<float>(centroid_sum / window_count)
            : 2000.0f;

        feat.spectral_flux = (flux_count > 0)
            ? static_cast<float>(flux_sum / flux_count)
            : 0.0f;
    }

    return feat;
}

// ──────────────────────────────────────────────────────────────
// Map features → spatial parameters
// ──────────────────────────────────────────────────────────────

inline void derive_spatial_params(AudioFeatures& feat) {
    // ── Wideness ────────────────────────────────────────────
    //
    // If the track is already wide, don't widen much.
    // If narrow, widen more. Respect mono compatibility.
    //
    // stereo_width: 0 = mono, ~0.5 = typical stereo, >0.5 = very wide
    //
    // Mapping:
    //   width 0.0  → wideness 140% (narrow, widen a lot)
    //   width 0.15 → wideness 125%
    //   width 0.3  → wideness 110%
    //   width 0.5  → wideness 100% (already good)
    //   width 0.7+ → wideness  90% (already wide, slight tighten)

    float w = feat.stereo_width;

    float wideness_float;
    if (w < 0.05f) {
        wideness_float = 140.0f; // essentially mono, widen significantly
    } else if (w < 0.5f) {
        // Linear interpolation: 140% at w=0 → 100% at w=0.5
        wideness_float = 140.0f - (w / 0.5f) * 40.0f;
    } else {
        // Already wide: gently reduce
        wideness_float = 100.0f - std::min((w - 0.5f) * 20.0f, 15.0f);
    }

    // If stereo correlation is very high (near-mono), be more conservative
    // to avoid phase issues.
    if (feat.stereo_correlation > 0.95f) {
        wideness_float = std::min(wideness_float, 110.0f);
    }

    // Clamp to 0-150%.
    wideness_float = std::max(0.0f, std::min(wideness_float, 150.0f));
    feat.suggested_wideness_permille = static_cast<uint16_t>(wideness_float * 10.0f);

    // ── Reverb preset selection ─────────────────────────────
    //
    // Based on spectral centroid (brightness) and flux (dynamics).
    //
    // Bright + percussive  → Plate (4) or Spring (5)
    // Bright + sustained   → Chamber (2)
    // Warm + sustained     → Hall (3)
    // Warm + percussive    → Room (1)
    // Very bright + short  → Ambience (6)

    float centroid = feat.spectral_centroid;
    float flux = feat.spectral_flux;

    // Normalização ajustada: flux típico de música real fica entre 100-10000
    // Usamos log para capturar dinâmica ampla sem saturar
    float flux_norm = std::min(std::log10(flux + 1.0f) / 4.0f, 1.0f);

    if (centroid > 6000.0f) {
        // Very bright.
        if (flux_norm > 0.5f) {
            feat.suggested_preset_id = 5; // Spring (percussive bright)
        } else {
            feat.suggested_preset_id = 4; // Plate (sustained bright)
        }
    } else if (centroid > 3000.0f) {
        // Medium-bright.
        if (flux_norm > 0.6f) {
            feat.suggested_preset_id = 1; // Room (percussive)
        } else {
            feat.suggested_preset_id = 2; // Chamber (sustained)
        }
    } else if (centroid > 1000.0f) {
        // Warm.
        if (flux_norm > 0.5f) {
            feat.suggested_preset_id = 1; // Room
        } else {
            feat.suggested_preset_id = 3; // Hall (warm sustained)
        }
    } else {
        // Dark / bass-heavy.
        feat.suggested_preset_id = 1; // Room (keep it tight)
    }

    // ── Reverb wet amount ───────────────────────────────────
    //
    // Percussive (high flux) → less reverb, but NOT 5%.
    // Ambient (low flux)     → more reverb.
    //
    // Adjusted range: 12–40% so the effect is always audible.

    float wet_float;
    if (flux_norm > 0.7f) {
        // Very percussive: 12–18%
        wet_float = 12.0f + (1.0f - flux_norm) * 20.0f;
    } else if (flux_norm > 0.3f) {
        // Moderate: 18–28%
        wet_float = 18.0f + (1.0f - flux_norm) * 14.0f;
    } else {
        // Ambient/sustained: 28–40%
        wet_float = 28.0f + (1.0f - flux_norm) * 12.0f;
    }

    // Scale down if the track is very loud (avoid muddying).
    if (feat.rms_energy > 0.3f) {
        wet_float *= 0.8f;
    }

    feat.suggested_reverb_wet_pct = static_cast<uint8_t>(
        std::max(12.0f, std::min(wet_float, 40.0f))
    );
}

// ──────────────────────────────────────────────────────────────
// Auto-spatial keyframe generation (with EMA and Hysteresis)
// ──────────────────────────────────────────────────────────────

inline std::vector<core::AutoKeyframe> generate_auto_keyframes(
    const std::vector<std::vector<int32_t>>& channels,
    uint8_t bits_per_sample,
    uint32_t sample_rate,
    float window_sec = 2.0f,
    float hop_sec = 1.0f,
    uint16_t wideness_threshold = 15, 
    uint8_t reverb_threshold = 3
) {
    std::vector<core::AutoKeyframe> keyframes;
    if (channels.empty() || channels[0].empty()) return keyframes;
    if (channels.size() < 2) return keyframes; // Automacao so para stereo por enquanto

    const size_t total_samples = channels[0].size();
    const size_t window_size = static_cast<size_t>(window_sec * sample_rate);
    const size_t hop_size = static_cast<size_t>(hop_sec * sample_rate);

    if (window_size == 0 || hop_size == 0) return keyframes;

    const std::vector<int32_t>& left = channels[0];
    const std::vector<int32_t>& right = channels[1];

    // EMA state (Exponential Moving Average para suavizar a analise)
    float ema_wideness = 1000.0f; // Comeca com 1000 (100%)
    float ema_reverb = 0.0f;
    float alpha = 0.4f; // 40% da leitura atual, 60% do historico

    // Last emitted keyframe state (para a histerese)
    int last_emitted_wideness = -1;
    int last_emitted_reverb = -1;

    for (size_t start = 0; start < total_samples; start += hop_size) {
        size_t end = std::min(start + window_size, total_samples);
        if (end <= start) break;

        std::vector<int32_t> win_l(left.begin() + start, left.begin() + end);
        std::vector<int32_t> win_r(right.begin() + start, right.begin() + end);

        AudioFeatures feat = analyze_audio(win_l, win_r, bits_per_sample, sample_rate);
        derive_spatial_params(feat);

        // EMA smoothing
        ema_wideness = alpha * feat.suggested_wideness_permille + (1.0f - alpha) * ema_wideness;
        ema_reverb   = alpha * feat.suggested_reverb_wet_pct   + (1.0f - alpha) * ema_reverb;

        uint16_t cur_wideness = static_cast<uint16_t>(std::round(ema_wideness));
        uint8_t  cur_reverb   = static_cast<uint8_t>(std::round(ema_reverb));

        // Hysteresis: only emit if changed significantly
        bool emit = false;
        if (keyframes.empty()) {
            emit = true;
        } else {
            int diff_w = std::abs(static_cast<int>(cur_wideness) - last_emitted_wideness);
            int diff_r = std::abs(static_cast<int>(cur_reverb) - last_emitted_reverb);
            if (diff_w >= wideness_threshold || diff_r >= reverb_threshold) {
                emit = true;
            }
        }

        if (emit) {
            keyframes.push_back({static_cast<uint32_t>(start), 0, static_cast<float>(cur_wideness)});
            keyframes.push_back({static_cast<uint32_t>(start), 1, static_cast<float>(cur_reverb)});
            last_emitted_wideness = cur_wideness;
            last_emitted_reverb = cur_reverb;
        }
    }

    return keyframes;
}

} // namespace slac::dsp
