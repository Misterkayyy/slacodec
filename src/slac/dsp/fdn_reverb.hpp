#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace slac::dsp {

// ──────────────────────────────────────────────────────────────
// Parametros sintetizados a partir dos descritores do registro (§2.6)
// ──────────────────────────────────────────────────────────────

struct FdnReverbParams {
    float rt60_s     = 0.60f;
    float predelay_s = 0.010f;
    float damp_hz    = 4000.0f;
    float size_scale = 1.00f;
    float wet        = 0.20f;
    bool  adaptive   = true;
};

inline FdnReverbParams reverb_params_from_descriptors(
    uint8_t category, uint8_t size, uint8_t decay, float wet_pct)
{
    FdnReverbParams p;
    p.wet = wet_pct / 100.0f;

    // RT60 pela classe de decaimento (tabela §2.6)
    switch (decay) {
        case 1: p.rt60_s = 0.25f; break;  // muito curto
        case 2: p.rt60_s = 0.55f; break;  // curto
        case 3: p.rt60_s = 1.10f; break;  // medio
        case 4: p.rt60_s = 2.20f; break;  // longo
        default: p.rt60_s = 0.60f; break;
    }

    // Porte -> escala dos delays + predelay
    switch (size) {
        case 1: p.size_scale = 0.60f; p.predelay_s = 0.005f; break;
        case 2: p.size_scale = 1.00f; p.predelay_s = 0.012f; break;
        case 3: p.size_scale = 1.60f; p.predelay_s = 0.025f; break;
        case 4: p.size_scale = 0.80f; p.predelay_s = 0.010f; break;
        default: p.size_scale = 1.00f; p.predelay_s = 0.010f; break;
    }

    // Categoria -> coloracao (damping) e ajustes finos
    switch (category) {
        case 1: p.damp_hz = 4200.0f; break;                    // room: seco
        case 2: p.damp_hz = 4600.0f; break;                    // chamber
        case 3: p.damp_hz = 3000.0f; break;                    // hall: escuro
        case 4: p.damp_hz = 6500.0f; break;                    // plate: brilhante
        case 5: p.damp_hz = 3500.0f; p.rt60_s *= 0.70f; break; // spring: metalico
        case 6: p.damp_hz = 5000.0f; p.rt60_s  = 0.22f; break; // ambience
        default: p.damp_hz = 4000.0f; break;
    }
    return p;
}

// ──────────────────────────────────────────────────────────────
// FDN 4a ordem por canal, feedback Hadamard ortogonal
// ──────────────────────────────────────────────────────────────

class FdnReverb {
public:
    static constexpr int N = 4;

    void configure(const FdnReverbParams& p, uint32_t sr) {
        sr_ = sr ? sr : 44100;
        params_ = p;

        static const float base_ms[2][N] = {
            {29.7f, 37.3f, 45.1f, 53.7f},   // L
            {30.9f, 38.6f, 46.2f, 55.4f},   // R (decorrelado)
        };

        float rt = p.rt60_s < 0.05f ? 0.05f : p.rt60_s;

        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < N; ++i) {
                float ms = base_ms[c][i] * p.size_scale;
                size_t len = static_cast<size_t>(ms * 0.001f * static_cast<float>(sr_));
                if (len < 8) len = 8;
                len_[c][i] = len;
                delay_[c][i].assign(len, 0.0f);
                pos_[c][i] = 0;
                lp_[c][i] = 0.0f;
                float t = static_cast<float>(len) / static_cast<float>(sr_);
                fb_[c][i] = std::pow(10.0f, -3.0f * t / rt); // -60 dB em rt60
            }
            size_t pre = static_cast<size_t>(p.predelay_s * static_cast<float>(sr_));
            predelay_[c].assign(pre + 1, 0.0f);
            pre_pos_[c] = 0;
        }

        float a = 1.0f - std::exp(-2.0f * 3.14159265358979f * p.damp_hz / static_cast<float>(sr_));
        damp_a_ = a < 0.01f ? 0.01f : (a > 1.0f ? 1.0f : a);
    }

    // wet_gain: nullptr (usa params_.wet) ou array por amostra (wet adaptativo)
    void process(const float* inL, const float* inR,
                 float* outL, float* outR, size_t n,
                 const float* wet_gain = nullptr)
    {
        const float cross = 0.35f;

        for (size_t s = 0; s < n; ++s) {
            float wl = wet_gain ? wet_gain[s] : params_.wet;

            float pl = push_predelay(0, inL[s]);
            float pr = push_predelay(1, inR[s]);

            float u[2] = { pl + cross * pr, pr + cross * pl };

            for (int c = 0; c < 2; ++c) {
                float lpv[N];
                for (int i = 0; i < N; ++i) lpv[i] = read(c, i);

                // damping one-pole + guarda de denormais (§5)
                for (int i = 0; i < N; ++i) {
                    lp_[c][i] += damp_a_ * (lpv[i] - lp_[c][i]);
                    if (std::fabs(lp_[c][i]) < 1e-20f) lp_[c][i] = 0.0f;
                    lpv[i] = lp_[c][i];
                }

                // Hadamard 4x4 * 0.5 com ganho por delay
                float g0 = lpv[0] * fb_[c][0];
                float g1 = lpv[1] * fb_[c][1];
                float g2 = lpv[2] * fb_[c][2];
                float g3 = lpv[3] * fb_[c][3];

                const float ig = 0.5f;
                write(c, 0, 0.5f * (g0 + g1 + g2 + g3) + ig * u[c]);
                write(c, 1, 0.5f * (g0 - g1 + g2 - g3) + ig * u[c]);
                write(c, 2, 0.5f * (g0 + g1 - g2 - g3) + ig * u[c]);
                write(c, 3, 0.5f * (g0 - g1 - g2 + g3) + ig * u[c]);

                if (c == 0) outL[s] = wl * 0.5f * (lpv[0] + lpv[1] - lpv[2] - lpv[3]);
                else        outR[s] = wl * 0.5f * (lpv[0] - lpv[1] + lpv[2] - lpv[3]);
            }
        }
    }

private:
    uint32_t sr_ = 44100;
    FdnReverbParams params_;
    std::vector<float> delay_[2][N];
    std::vector<float> predelay_[2];
    size_t pos_[2][N]     = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    size_t pre_pos_[2]    = {0, 0};
    size_t len_[2][N]     = {{8, 8, 8, 8}, {8, 8, 8, 8}};
    float  lp_[2][N]      = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    float  fb_[2][N]      = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    float  damp_a_        = 0.5f;

    float push_predelay(int c, float x) {
        size_t sz = predelay_[c].size();
        predelay_[c][pre_pos_[c]] = x;
        float out = predelay_[c][(pre_pos_[c] + 1) % sz];
        pre_pos_[c] = (pre_pos_[c] + 1) % sz;
        return out;
    }

    float read(int c, int i) { return delay_[c][i][pos_[c][i]]; }

    void write(int c, int i, float v) {
        delay_[c][i][pos_[c][i]] = v;
        pos_[c][i] = (pos_[c][i] + 1) % len_[c][i];
    }
};

// ──────────────────────────────────────────────────────────────
// Wrapper offline com wet adaptativo por bloco (rampa anti-zipper, §4.3)
// ──────────────────────────────────────────────────────────────

inline void apply_fdn_reverb_offline(
    std::vector<int32_t>& pcmL,
    std::vector<int32_t>& pcmR,
    const FdnReverbParams& params,
    uint32_t sr,
    bool adaptive)
{
    size_t n = pcmL.size();
    if (n == 0 || params.wet <= 0.0f) return;

    FdnReverb rev;
    rev.configure(params, sr);

    const size_t BLOCK = 4096;
    std::vector<float> wet_gain(n, params.wet);

    if (adaptive) {
        float prev_m = 1.0f;
        for (size_t b0 = 0; b0 < n; b0 += BLOCK) {
            size_t b1 = std::min(n, b0 + BLOCK);
            double e = 0.0, d = 0.0;
            float prev = 0.0f;
            for (size_t i = b0; i < b1; ++i) {
                float x = static_cast<float>(pcmL[i] + pcmR[i]) * 0.5f;
                e += static_cast<double>(x) * x;
                float diff = x - prev;
                prev = x;
                d += static_cast<double>(diff) * diff;
            }
            float hf = (e > 1e-6) ? static_cast<float>(d / e) : 0.0f;
            float t = hf > 1.0f ? 1.0f : hf;
            float target = 1.0f - 0.5f * t;   // transiente/brilhante -> menos reverb

            size_t len = b1 - b0;
            for (size_t i = 0; i < len; ++i) {
                float m = prev_m + (target - prev_m) *
                            (static_cast<float>(i) / static_cast<float>(len));
                wet_gain[b0 + i] = params.wet * m;
            }
            prev_m = target;
        }
    }

    const float norm = 1.0f / 32768.0f;
    std::vector<float> inL(n), inR(n), outL(n), outR(n);
    for (size_t i = 0; i < n; ++i) {
        inL[i] = static_cast<float>(pcmL[i]) * norm;
        inR[i] = static_cast<float>(pcmR[i]) * norm;
    }

    rev.process(inL.data(), inR.data(), outL.data(), outR.data(), n, wet_gain.data());

    for (size_t i = 0; i < n; ++i) {
        float l = inL[i] + outL[i];
        float r = inR[i] + outR[i];
        if (l >  1.0f) l =  1.0f;
        if (l < -1.0f) l = -1.0f;
        if (r >  1.0f) r =  1.0f;
        if (r < -1.0f) r = -1.0f;
        pcmL[i] = static_cast<int32_t>(l * 32767.0f);
        pcmR[i] = static_cast<int32_t>(r * 32767.0f);
    }
}

// Wet adaptativo por bloco para cadeia float (rampa anti-zipper, §4.3)
inline void compute_adaptive_wet_gain_float(
    const float* L, const float* R, size_t n, float wet, float* out)
{
    const size_t BLOCK = 4096;
    float prev_m = 1.0f;
    for (size_t b0 = 0; b0 < n; b0 += BLOCK) {
        size_t b1 = std::min(n, b0 + BLOCK);
        double e = 0.0, d = 0.0;
        float prev = 0.0f;
        for (size_t i = b0; i < b1; ++i) {
            float x = (L[i] + R[i]) * 0.5f;
            e += static_cast<double>(x) * x;
            float diff = x - prev;
            prev = x;
            d += static_cast<double>(diff) * diff;
        }
        float hf = (e > 1e-6) ? static_cast<float>(d / e) : 0.0f;
        float t = hf > 1.0f ? 1.0f : hf;
        float target = 1.0f - 0.5f * t;
        size_t len = b1 - b0;
        for (size_t i = 0; i < len; ++i) {
            float m = prev_m + (target - prev_m) *
                        (static_cast<float>(i) / static_cast<float>(len));
            out[b0 + i] = wet * m;
        }
        prev_m = target;
    }
}

} // namespace slac::dsp
