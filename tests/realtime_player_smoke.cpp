#include "slac/rt/realtime_player.hpp"
#include "slac/dsp/spatial_chain.hpp"
#include "slac/core/container.hpp"

#include <chrono>
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

    // ── Configuração: HRIR + widening (sem reverb, sem limiter) ──
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

    dsp::SpatialChainConfig cfg;
    cfg.hrir = &dummy_hrir;
    cfg.hrir_unity_gain = true;
    cfg.wideness = 1.25f;
    cfg.reverb_enabled = false;
    cfg.limit_mode = dsp::SpatialChainConfig::LimitMode::None;
    cfg.use_auto = false;

    // ── Render via RealtimePlayer (pipeline multi-thread) ────
    RealtimePlayer player;
    if (!player.open(path, cfg, 128)) {
        std::cerr << "FAIL: could not open " << path << "\n";
        return 1;
    }

    auto t0 = std::chrono::steady_clock::now();
    auto stream_pcm = player.render_all();
    auto t1 = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << "[TEST] RealtimePlayer render\n";
    std::cout << "  samples/ch: " << stream_pcm[0].size() << "\n";
    std::cout << "  elapsed: " << elapsed_ms << " ms\n";

    // Throughput: samples/segundo processados.
    double throughput = (stream_pcm[0].size() * 2.0) / (elapsed_ms / 1000.0);
    double realtime_ratio = throughput / (player.sample_rate() * 2.0);
    std::cout << "  throughput: " << throughput << " samples/s\n";
    std::cout << "  realtime ratio: " << realtime_ratio << "x\n";

    // ── Referência offline ───────────────────────────────────
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> file_data((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    SlacFormat fmt;
    auto full_pcm = decodeSlacFile(file_data, &fmt);

    std::vector<int32_t> offline_L = full_pcm[0];
    std::vector<int32_t> offline_R = full_pcm[1];
    dsp::apply_spatial_chain(offline_L, offline_R, fmt.bitsPerSample, fmt.sampleRate, cfg);

    // ── Comparação (streaming float vs offline int32) ────────
    const float q = static_cast<float>((1 << (fmt.bitsPerSample - 1)) - 1);
    size_t cmp_len = std::min(stream_pcm[0].size(), offline_L.size());

    // Debug: range dos sinais.
    {
        float smin = 1e30f, smax = -1e30f;
        for (size_t i = 0; i < stream_pcm[0].size(); ++i) {
            smin = std::min(smin, stream_pcm[0][i]);
            smax = std::max(smax, stream_pcm[0][i]);
        }
        int32_t omin = offline_L[0], omax = offline_L[0];
        for (size_t i = 0; i < offline_L.size(); ++i) {
            omin = std::min(omin, offline_L[i]);
            omax = std::max(omax, offline_L[i]);
        }
        std::cout << "[DEBUG] stream_pcm[0] range: [" << smin << ", " << smax << "]\n";
        std::cout << "[DEBUG] offline_L range: [" << omin << ", " << omax << "]\n";

        // Primeira diferença.
        for (size_t i = 0; i < cmp_len; ++i) {
            int32_t sL = static_cast<int32_t>(std::lround(stream_pcm[0][i] * q));
            if (sL != offline_L[i]) {
                std::cout << "[DEBUG] first diff at i=" << i
                          << " stream_q=" << sL << " offline=" << offline_L[i]
                          << " stream_raw=" << stream_pcm[0][i] << "\n";
                break;
            }
        }
    }

    size_t diff_count = 0;
    int32_t max_diff = 0;
    for (size_t i = 0; i < cmp_len; ++i) {
        int32_t sL = static_cast<int32_t>(std::lround(stream_pcm[0][i] * q));
        int32_t sR = static_cast<int32_t>(std::lround(stream_pcm[1][i] * q));
        if (sL != offline_L[i]) ++diff_count;
        if (sR != offline_R[i]) ++diff_count;
        int32_t dL = std::abs(sL - offline_L[i]);
        int32_t dR = std::abs(sR - offline_R[i]);
        max_diff = std::max(max_diff, std::max(dL, dR));
    }

    float diff_pct = 100.0f * diff_count / (2.0f * cmp_len);
    std::cout << "[TEST] pipeline vs offline\n";
    std::cout << "  samples compared: " << cmp_len << "\n";
    std::cout << "  different samples: " << diff_count << " (" << diff_pct << "%)\n";
    std::cout << "  max abs diff: " << max_diff << "\n";

    bool ok = (max_diff <= 1) && (diff_pct < 0.5f);
    std::cout << "[TEST] pipeline vs offline ok=" << (ok ? "yes" : "no") << "\n";
    if (!ok) {
        std::cerr << "FAIL: pipeline vs offline\n";
        ++failures;
    }

    // Tempo real: throughput deve ser > 1x (idealmente muito mais).
    bool rt_ok = (realtime_ratio > 1.0);
    std::cout << "[TEST] realtime-capable=" << (rt_ok ? "yes" : "no") << "\n";
    if (!rt_ok) {
        std::cerr << "WARN: not realtime-capable (ratio < 1x)\n";
    }

    if (failures == 0) {
        std::cout << "\n=== REALTIME PLAYER SMOKE TESTS PASSED ===\n";
        return 0;
    }
    std::cerr << "\n=== " << failures << " TEST(S) FAILED ===\n";
    return 1;
}
