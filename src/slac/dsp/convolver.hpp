#pragma once

#include <cmath>
#include <cstring>
#include <vector>

namespace slac::dsp {

// ── Single-channel direct convolution ───────────────────────

inline void convolve_direct(
    const float* input, size_t input_len,
    const float* ir, size_t ir_len,
    float* output, size_t output_len
) {
    std::memset(output, 0, output_len * sizeof(float));

    for (size_t n = 0; n < output_len; ++n) {
        float sum = 0.0f;

        size_t k_start = (n >= input_len) ? (n - input_len + 1) : 0;
        size_t k_end   = (n < ir_len) ? n : (ir_len - 1);

        for (size_t k = k_start; k <= k_end; ++k) {
            sum += input[n - k] * ir[k];
        }

        output[n] = sum;
    }
}

// ── True-stereo IR: 4 independent impulse responses ────────
//
// Channel layout (matches brainstorm §4.3 and the user's IR):
//   ll (ch 0) = Left  input → Left  output  (direct,  30°)
//   lr (ch 1) = Left  input → Right output  (crossfeed, 210°)
//   rl (ch 2) = Right input → Left  output  (crossfeed, 150°)
//   rr (ch 3) = Right input → Right output  (direct, 330°)
//
// True-stereo convolution:
//   out_L = conv(in_L, ll) + conv(in_R, rl)
//   out_R = conv(in_L, lr) + conv(in_R, rr)

struct TrueStereoIR {
    std::vector<float> ll;  // ch 0: L→L
    std::vector<float> lr;  // ch 1: L→R
    std::vector<float> rl;  // ch 2: R→L
    std::vector<float> rr;  // ch 3: R→R
    uint32_t sample_rate = 0;

    size_t length() const { return ll.size(); }
    bool empty() const { return ll.empty(); }
};

// ── Normalize all 4 channels by global peak ─────────────────
//
// Preserves relative balance between channels.
inline void normalize_ir(TrueStereoIR& ir) {
    float peak = 0.0f;

    for (float s : ir.ll) peak = std::max(peak, std::fabs(s));
    for (float s : ir.lr) peak = std::max(peak, std::fabs(s));
    for (float s : ir.rl) peak = std::max(peak, std::fabs(s));
    for (float s : ir.rr) peak = std::max(peak, std::fabs(s));

    if (peak < 1e-8f) return;

    float scale = 1.0f / peak;
    for (auto& s : ir.ll) s *= scale;
    for (auto& s : ir.lr) s *= scale;
    for (auto& s : ir.rl) s *= scale;
    for (auto& s : ir.rr) s *= scale;
}

// ── 4-path true-stereo convolution ──────────────────────────

inline void convolve_true_stereo(
    const std::vector<int32_t>& in_left,
    const std::vector<int32_t>& in_right,
    int bits_per_sample,
    const TrueStereoIR& ir_in,
    std::vector<int32_t>& out_left,
    std::vector<int32_t>& out_right
) {
    if (ir_in.empty()) {
        out_left = in_left;
        out_right = in_right;
        return;
    }

    // Normalize.
    TrueStereoIR ir = ir_in;
    normalize_ir(ir);

    size_t input_len  = in_left.size();
    size_t ir_len     = ir.length();
    size_t output_len = input_len + ir_len - 1;

    float norm = 1.0f / static_cast<float>(1 << (bits_per_sample - 1));

    std::vector<float> in_l(input_len), in_r(input_len);
    for (size_t i = 0; i < input_len; ++i) {
        in_l[i] = static_cast<float>(in_left[i])  * norm;
        in_r[i] = static_cast<float>(in_right[i]) * norm;
    }

    // 4 convolution paths.
    std::vector<float> path_ll(output_len, 0.0f);
    std::vector<float> path_lr(output_len, 0.0f);
    std::vector<float> path_rl(output_len, 0.0f);
    std::vector<float> path_rr(output_len, 0.0f);

    convolve_direct(in_l.data(), input_len, ir.ll.data(), ir_len, path_ll.data(), output_len);
    convolve_direct(in_l.data(), input_len, ir.lr.data(), ir_len, path_lr.data(), output_len);
    convolve_direct(in_r.data(), input_len, ir.rl.data(), ir_len, path_rl.data(), output_len);
    convolve_direct(in_r.data(), input_len, ir.rr.data(), ir_len, path_rr.data(), output_len);

    // Sum paths.
    float max_val = static_cast<float>((1 << (bits_per_sample - 1)) - 1);
    float min_val = -static_cast<float>(1 << (bits_per_sample - 1));

    out_left.resize(output_len);
    out_right.resize(output_len);

    for (size_t i = 0; i < output_len; ++i) {
        float L = path_ll[i] + path_rl[i];
        float R = path_lr[i] + path_rr[i];

        // Clamp to [-1, 1].
        if (L >  1.0f) L =  1.0f;
        if (L < -1.0f) L = -1.0f;
        if (R >  1.0f) R =  1.0f;
        if (R < -1.0f) R = -1.0f;

        float L_int = L * max_val;
        float R_int = R * max_val;

        if (L_int > max_val) L_int = max_val;
        if (L_int < min_val) L_int = min_val;
        if (R_int > max_val) R_int = max_val;
        if (R_int < min_val) R_int = min_val;

        out_left[i]  = static_cast<int32_t>(L_int);
        out_right[i] = static_cast<int32_t>(R_int);
    }
}

} // namespace slac::dsp
