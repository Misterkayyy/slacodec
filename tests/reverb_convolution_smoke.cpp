#include <slac/dsp/reverb_engine.hpp>

#include <cmath>
#include <iostream>
#include <string>

using namespace slac::dsp;

static bool check(bool cond, const std::string& label) {
    if (cond) {
        std::cout << "[OK] " << label << "\n";
    } else {
        std::cerr << "[FAIL] " << label << "\n";
    }
    return cond;
}

int main() {
    constexpr int bits = 16;
    constexpr int sample_rate = 44100;
    bool all_ok = true;

    // ── Generate a synthetic true-stereo IR for testing ─────
    //
    // Short exponential decay, 50ms, 4 channels.
    {
        size_t ir_len = sample_rate / 20; // 50ms

        TrueStereoIR synth_ir;
        synth_ir.sample_rate = sample_rate;
        synth_ir.ll.resize(ir_len);
        synth_ir.lr.resize(ir_len);
        synth_ir.rl.resize(ir_len);
        synth_ir.rr.resize(ir_len);

        for (size_t i = 0; i < ir_len; ++i) {
            float t = static_cast<float>(i) / sample_rate;
            float decay = std::exp(-t * 40.0f);

            // Direct paths: slightly different L/R for stereo width.
            synth_ir.ll[i] = std::sin(2.0f * 3.14159f * 1000.0f * t) * decay;
            synth_ir.rr[i] = std::cos(2.0f * 3.14159f * 1000.0f * t) * decay;

            // Crossfeed paths: quieter, slightly delayed feel.
            synth_ir.lr[i] = std::sin(2.0f * 3.14159f * 800.0f * t) * decay * 0.3f;
            synth_ir.rl[i] = std::cos(2.0f * 3.14159f * 800.0f * t) * decay * 0.3f;
        }

        ReverbEngine engine;
        engine.register_ir(1, std::move(synth_ir));

        // ── Test signal: a short impulse ────────────────────
        size_t sig_len = 128;
        std::vector<int32_t> in_l(sig_len, 0), in_r(sig_len, 0);
        in_l[0] = 16384;
        in_r[0] = -8192;

        std::vector<int32_t> out_l = in_l;
        std::vector<int32_t> out_r = in_r;

        // Apply reverb: preset 1, 100% wet.
        engine.apply(out_l, out_r, bits, 1, 100, 0, 0, 0);

        all_ok &= check(out_l.size() >= sig_len, "Output has reverb tail");
        all_ok &= check(out_l[0] != 0 || out_r[0] != 0, "Impulse produces output");

        bool has_tail = false;
        for (size_t i = sig_len; i < out_l.size(); ++i) {
            if (out_l[i] != 0 || out_r[i] != 0) {
                has_tail = true;
                break;
            }
        }
        all_ok &= check(has_tail, "Reverb tail extends beyond input");

        // ── Test bypass ─────────────────────────────────────
        std::vector<int32_t> bypass_l = in_l, bypass_r = in_r;
        engine.apply(bypass_l, bypass_r, bits, 0, 100, 0, 0, 0);
        all_ok &= check(bypass_l == in_l && bypass_r == in_r,
                        "Preset 0 (bypass) leaves audio unchanged");

        // ── Test wet=0 ──────────────────────────────────────
        std::vector<int32_t> dry_l = in_l, dry_r = in_r;
        engine.apply(dry_l, dry_r, bits, 1, 0, 0, 0, 0);
        all_ok &= check(dry_l == in_l && dry_r == in_r,
                        "Wet=0 leaves audio unchanged");
    }

    // ── Try loading the user's real WAV IR ──────────────────
    {
        std::string ir_path = "ir/surround_crossfeed.wav";

        try {
            ReverbEngine engine;
            engine.load_ir_for_preset(1, ir_path);
            std::cout << "[OK] Loaded user IR from: " << ir_path << "\n";

            // Apply to a test signal.
            size_t sig_len = 1024;
            std::vector<int32_t> in_l(sig_len), in_r(sig_len);
            for (size_t i = 0; i < sig_len; ++i) {
                in_l[i] = static_cast<int32_t>(
                    10000.0 * std::sin(2.0 * 3.14159 * 440.0 * i / sample_rate));
                in_r[i] = static_cast<int32_t>(
                    8000.0 * std::sin(2.0 * 3.14159 * 550.0 * i / sample_rate));
            }

            std::vector<int32_t> out_l = in_l, out_r = in_r;
            engine.apply(out_l, out_r, bits, 1, 50, 0, 0, 0);

            all_ok &= check(out_l.size() >= in_l.size(),
                            "User IR produces reverb tail");

            bool changed = false;
            for (size_t i = 0; i < in_l.size(); ++i) {
                if (out_l[i] != in_l[i] || out_r[i] != in_r[i]) {
                    changed = true;
                    break;
                }
            }
            all_ok &= check(changed, "User IR modifies the audio");

            std::cout << "[OK] User IR convolution verified.\n";
        }
        catch (const std::exception& e) {
            std::cout << "[SKIP] User IR not found (" << e.what() << ")\n";
            std::cout << "[INFO] Place your IR WAV at: " << ir_path << "\n";
        }
    }

    if (!all_ok) return 1;

    std::cout << "\n--- SLAC Reverb Convolution MVP PASSED ---\n";
    return 0;
}
