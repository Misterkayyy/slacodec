#pragma once

#include "convolver.hpp"
#include "fdn_reverb.hpp"
#include "reverb_registry.hpp"
#include "wav_loader.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace slac::dsp {

class ReverbEngine {
public:
    void set_synth_fallback(bool b) { synth_ = b; }
    bool last_used_synth() const { return last_synth_; }

    // Carrega IR de convolucao (4 canais true-stereo, 2 = dual-mono, 1 = mono).
    void load_ir_for_preset(uint16_t preset_id, const std::string& wav_path) {
        WavData wav = load_wav(wav_path);
        auto ch = wav.channels();

        TrueStereoIR ir;
        ir.sample_rate = wav.sample_rate;

        if (ch.empty()) throw std::runtime_error("ReverbEngine: WAV has no channels");

        if (ch.size() == 1) {
            ir.ll = ir.lr = ir.rl = ir.rr = ch[0];
        } else if (ch.size() == 2) {
            ir.ll = ch[0];
            ir.rr = ch[1];
            ir.lr.assign(ch[0].size(), 0.0f);
            ir.rl.assign(ch[1].size(), 0.0f);
        } else {
            ir.ll = ch[0];
            ir.lr = ch[1];
            ir.rl = ch[2];
            ir.rr = ch[3];
        }

        set_ir(preset_id, std::move(ir));
    }

    void register_ir(uint16_t preset_id, TrueStereoIR ir) {
        set_ir(preset_id, std::move(ir));
    }

    // Aplica reverb: IR de convolucao se houver; senao FDN synth adaptativo
    // sintetizado dos descritores de fallback (§2.4/§2.6).
    void apply(
        std::vector<int32_t>& pcm_left,
        std::vector<int32_t>& pcm_right,
        int bits_per_sample,
        uint16_t preset_id,
        uint8_t wet_pct,
        uint8_t fb_category,
        uint8_t fb_size,
        uint8_t fb_decay,
        uint32_t sample_rate = 44100,
        bool adaptive = true)
    {
        last_synth_ = false;
        if (preset_id == 0 || wet_pct == 0) return;

        ResolvedPreset resolved = registry_.resolve(
            preset_id, fb_category, fb_size, fb_decay);
        if (resolved.is_bypass) return;

        TrueStereoIR* ir = find_ir(resolved.resolved_id);
        if (ir != nullptr && !ir->empty()) {
            // Caminho 1: convolucao true-stereo com IR da biblioteca.
            std::vector<int32_t> wet_l, wet_r;
            convolve_true_stereo(pcm_left, pcm_right, bits_per_sample,
                                 *ir, wet_l, wet_r);

            float wet = static_cast<float>(wet_pct) / 100.0f;
            float dry = 1.0f - wet;
            size_t orig = pcm_left.size();
            size_t out_len = wet_l.size();

            pcm_left.resize(out_len, 0);
            pcm_right.resize(out_len, 0);

            float max_val = static_cast<float>((1 << (bits_per_sample - 1)) - 1);
            float min_val = -static_cast<float>(1 << (bits_per_sample - 1));

            for (size_t i = 0; i < out_len; ++i) {
                float d_l = (i < orig) ? static_cast<float>(pcm_left[i]) : 0.0f;
                float d_r = (i < orig) ? static_cast<float>(pcm_right[i]) : 0.0f;
                float m_l = dry * d_l + wet * static_cast<float>(wet_l[i]);
                float m_r = dry * d_r + wet * static_cast<float>(wet_r[i]);
                if (m_l > max_val) m_l = max_val;
                if (m_l < min_val) m_l = min_val;
                if (m_r > max_val) m_r = max_val;
                if (m_r < min_val) m_r = min_val;
                pcm_left[i]  = static_cast<int32_t>(m_l);
                pcm_right[i] = static_cast<int32_t>(m_r);
            }
            return;
        }

        if (synth_) {
            // Caminho 2: FDN synth adaptativo a partir dos descritores.
            FdnReverbParams p = reverb_params_from_descriptors(
                fb_category, fb_size, fb_decay, static_cast<float>(wet_pct));
            p.adaptive = adaptive;
            apply_fdn_reverb_offline(pcm_left, pcm_right, p, sample_rate, adaptive);
            last_synth_ = true;
        }
    }

    const PresetRegistry& registry() const { return registry_; }

private:
    PresetRegistry registry_;
    bool synth_ = true;
    bool last_synth_ = false;

    struct IREntry {
        uint16_t preset_id;
        TrueStereoIR ir;
    };
    std::vector<IREntry> irs_;

    TrueStereoIR* find_ir(uint16_t id) {
        for (auto& e : irs_) {
            if (e.preset_id == id) return &e.ir;
        }
        return nullptr;
    }

    void set_ir(uint16_t id, TrueStereoIR ir) {
        for (auto& e : irs_) {
            if (e.preset_id == id) { e.ir = std::move(ir); return; }
        }
        irs_.push_back({id, std::move(ir)});
    }
};

} // namespace slac::dsp
