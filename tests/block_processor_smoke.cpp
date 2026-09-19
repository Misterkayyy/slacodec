#include "slac/rt/block_processor.hpp"
#include "slac/dsp/spatial_chain.hpp"
#include "slac/core/container.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

using namespace slac;
using namespace slac::rt;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file.slac>\n";
        return 1;
    }

    int failures = 0;
    const char* path = argv[1];

    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> file_data((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());

    SlacFormat fmt;
    SpatMetadata spat;
    std::vector<core::AutoKeyframe> auto_kfs;
    auto pcm = decodeSlacFile(file_data, &fmt, &spat, nullptr, nullptr, true, &auto_kfs);

    if (pcm.size() != 2) {
        std::cerr << "FAIL: expected 2 channels, got " << pcm.size() << "\n";
        return 1;
    }

    // Configuração MINIMAL: apenas widening (sem HRIR, sem reverb, sem limiter).
    // Isso isola o processamento em blocos sem latência de convolução.
    dsp::SpatialChainConfig cfg;
    cfg.hrir = nullptr;                                    // sem HRIR
    cfg.wideness = 1.25f;
    cfg.reverb_enabled = false;                            // sem reverb
    cfg.limit_mode = dsp::SpatialChainConfig::LimitMode::None;  // sem limiter
    cfg.use_auto = false;

    // ── Offline ───────────────────────────────────────────────
    std::vector<int32_t> offline_L = pcm[0];
    std::vector<int32_t> offline_R = pcm[1];
    dsp::apply_spatial_chain(offline_L, offline_R, fmt.bitsPerSample, fmt.sampleRate, cfg);

    // ── Blocos ────────────────────────────────────────────────
    size_t block_size = 128;
    BlockProcessor proc;
    proc.configure(cfg, fmt.sampleRate, block_size);

    const float scale = 1.0f / static_cast<float>(1 << (fmt.bitsPerSample - 1));
    size_t n = pcm[0].size();
    std::vector<float> inL(n), inR(n);
    for (size_t i = 0; i < n; ++i) {
        inL[i] = static_cast<float>(pcm[0][i]) * scale;
        inR[i] = static_cast<float>(pcm[1][i]) * scale;
    }

    std::vector<float> stream_L, stream_R;
    std::vector<float> block_inL(block_size), block_inR(block_size);
    std::vector<float> block_outL(block_size), block_outR(block_size);

    size_t num_blocks = (n + block_size - 1) / block_size;
    for (size_t b = 0; b < num_blocks; ++b) {
        for (size_t i = 0; i < block_size; ++i) {
            size_t idx = b * block_size + i;
            block_inL[i] = (idx < n) ? inL[idx] : 0.0f;
            block_inR[i] = (idx < n) ? inR[idx] : 0.0f;
        }
        proc.process_block(block_inL.data(), block_inR.data(),
                           block_outL.data(), block_outR.data());
        stream_L.insert(stream_L.end(), block_outL.begin(), block_outL.end());
        stream_R.insert(stream_R.end(), block_outR.begin(), block_outR.end());
    }

    // Quantização final.
    const float q = static_cast<float>((1 << (fmt.bitsPerSample - 1)) - 1);
    std::vector<int32_t> stream_L_int(n), stream_R_int(n);
    for (size_t i = 0; i < n; ++i) {
        float l = stream_L[i] * q;
        float r = stream_R[i] * q;
        if (l > q) l = q; if (l < -q) l = -q;
        if (r > q) r = q; if (r < -q) r = -q;
        stream_L_int[i] = static_cast<int32_t>(std::lround(l));
        stream_R_int[i] = static_cast<int32_t>(std::lround(r));
    }

    // ── Comparação ────────────────────────────────────────────
    size_t cmp_len = std::min(offline_L.size(), stream_L_int.size());
    size_t diff_count = 0;
    int32_t max_diff = 0;
    for (size_t i = 0; i < cmp_len; ++i) {
        if (offline_L[i] != stream_L_int[i]) ++diff_count;
        if (offline_R[i] != stream_R_int[i]) ++diff_count;
        int32_t dL = std::abs(offline_L[i] - stream_L_int[i]);
        int32_t dR = std::abs(offline_R[i] - stream_R_int[i]);
        max_diff = std::max(max_diff, std::max(dL, dR));
    }

    float diff_pct = 100.0f * diff_count / (2.0f * cmp_len);
    std::cout << "[TEST] widening-only: offline vs block-processing\n";
    std::cout << "  samples compared: " << cmp_len << "\n";
    std::cout << "  different samples: " << diff_count << " (" << diff_pct << "%)\n";
    std::cout << "  max abs diff: " << max_diff << "\n";

    bool ok = (max_diff <= 1) && (diff_pct < 0.1f);
    std::cout << "[TEST] widening-only ok=" << (ok ? "yes" : "no") << "\n";
    if (!ok) {
        std::cerr << "FAIL: widening-only\n";
        ++failures;
    }

    // ── Teste C: HRIR particionado vs convolução direta ─────
    {
        dsp::TrueStereoIR dummy_hrir;
        size_t ir_len = 64;
        dummy_hrir.ll.resize(ir_len); dummy_hrir.lr.resize(ir_len);
        dummy_hrir.rl.resize(ir_len); dummy_hrir.rr.resize(ir_len);
        for (size_t i = 0; i < ir_len; ++i) {
            float decay = std::exp(-0.05f * i);
            dummy_hrir.ll[i] = decay * 0.8f;
            dummy_hrir.lr[i] = decay * 0.1f;
            dummy_hrir.rl[i] = decay * 0.1f;
            dummy_hrir.rr[i] = decay * 0.8f;
        }

        dsp::SpatialChainConfig cfg_hrir;
        cfg_hrir.hrir = &dummy_hrir;
        cfg_hrir.hrir_unity_gain = false;
        cfg_hrir.wideness = 1.0f;     // sem widening (isola o HRIR)
        cfg_hrir.reverb_enabled = false;
        cfg_hrir.limit_mode = dsp::SpatialChainConfig::LimitMode::None;
        cfg_hrir.use_auto = false;

        // Referência: convolução direta (verdade absoluta).
        std::vector<float> refL, refR;
        dsp::conv_true_stereo_float(inL, inR, dummy_hrir, refL, refR);

        // Streaming: BlockProcessor com HRIR.
        BlockProcessor proc_hrir;
        proc_hrir.configure(cfg_hrir, fmt.sampleRate, block_size);

        std::vector<float> stream_hrir_L, stream_hrir_R;
        for (size_t b = 0; b < num_blocks; ++b) {
            for (size_t i = 0; i < block_size; ++i) {
                size_t idx = b * block_size + i;
                block_inL[i] = (idx < n) ? inL[idx] : 0.0f;
                block_inR[i] = (idx < n) ? inR[idx] : 0.0f;
            }
            proc_hrir.process_block(block_inL.data(), block_inR.data(),
                                    block_outL.data(), block_outR.data());
            stream_hrir_L.insert(stream_hrir_L.end(), block_outL.begin(), block_outL.end());
            stream_hrir_R.insert(stream_hrir_R.end(), block_outR.begin(), block_outR.end());
        }

        // Encontra o melhor offset (compensa latência da convolução particionada).
        size_t best_offset = 0;
        double best_err = 1e30;
        size_t max_offset = 2 * block_size;
        size_t window = std::min(size_t(20000), std::min(n, refL.size()));
        for (size_t offset = 0; offset < max_offset; ++offset) {
            double err = 0.0;
            size_t count = 0;
            for (size_t i = 0; i + offset < window && i < stream_hrir_L.size(); ++i) {
                double d = static_cast<double>(refL[i]) - stream_hrir_L[i + offset];
                err += d * d;
                ++count;
            }
            if (count > 0) {
                err /= count;
                if (err < best_err) { best_err = err; best_offset = offset; }
            }
        }

        // Compara tudo com o melhor offset.
        size_t valid_samples = std::min(n, refL.size()) - best_offset;
        size_t diff_count = 0;
        float max_diff = 0.0f;
        for (size_t i = 0; i < valid_samples && i + best_offset < stream_hrir_L.size(); ++i) {
            float dL = std::fabs(refL[i] - stream_hrir_L[i + best_offset]);
            float dR = std::fabs(refR[i] - stream_hrir_R[i + best_offset]);
            if (dL > 1e-4f || dR > 1e-4f) ++diff_count;
            max_diff = std::max(max_diff, std::max(dL, dR));
        }

        float diff_pct = 100.0f * diff_count / valid_samples;
        std::cout << "[TEST] HRIR streaming vs direct\n";
        std::cout << "  best_offset: " << best_offset << "\n";
        std::cout << "  valid_samples: " << valid_samples << "\n";
        std::cout << "  different samples (>1e-4): " << diff_count << " (" << diff_pct << "%)\n";
        std::cout << "  max abs diff: " << max_diff << "\n";

        bool ok = (max_diff < 1e-3f) && (diff_pct < 0.1f);
        std::cout << "[TEST] HRIR streaming ok=" << (ok ? "yes" : "no") << "\n";
        if (!ok) {
            std::cerr << "FAIL: HRIR streaming\n";
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "\n=== BLOCK PROCESSOR SMOKE TESTS PASSED ===\n";
        return 0;
    }
    std::cerr << "\n=== " << failures << " TEST(S) FAILED ===\n";
    return 1;
}
