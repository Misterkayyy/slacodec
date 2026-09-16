#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace slac::dsp {

// Limiter brickwall offline com lookahead (stereo, ganho compartilhado
// entre canais para preservar a imagem estereo).
//
// 1) gneed[n] = ganho necessario para ficar no ceiling
// 2) min deslizante hacia atras (lookahead) -> reducao comeca antes do pico
// 3) suavizacao forward: release lento p/ recuperar, attack rapido p/ cair
//    (sem zipper noise: ganho varia continuo, §4.3)
inline float limit_stereo_float(
    std::vector<float>& L,
    std::vector<float>& R,
    uint32_t sr,
    float ceiling,
    float lookahead_s,
    float release_s,
    float* out_max_reduction_db = nullptr)
{
    size_t n = L.size();
    if (n == 0) return 0.0f;

    size_t look = static_cast<size_t>(lookahead_s * static_cast<float>(sr));
    if (look < 1) look = 1;

    const float release_rate = 1.0f / std::max(1.0f, release_s * static_cast<float>(sr));
    const float attack_rate  = 1.0f / std::max(1.0f, 0.001f * static_cast<float>(sr));

    std::vector<float> gneed(n), g2(n), g(n);

    for (size_t i = 0; i < n; ++i) {
        float p = std::max(std::fabs(L[i]), std::fabs(R[i]));
        gneed[i] = (p > ceiling) ? (ceiling / p) : 1.0f;
    }

    // min deslizante em janela [i, i+look], iterando de tras p/ frente
    std::deque<size_t> dq;
    for (size_t ii = n; ii-- > 0; ) {
        size_t i = ii;
        while (!dq.empty() && gneed[dq.back()] >= gneed[i]) dq.pop_back();
        dq.push_back(i);
        while (dq.front() > i + look) dq.pop_front();
        g2[i] = gneed[dq.front()];
    }

    // suavizacao forward (release/attack)
    g[0] = g2[0];
    for (size_t i = 1; i < n; ++i) {
        float v = g[i - 1] + release_rate;   // recupera devagar
        if (v > 1.0f) v = 1.0f;
        if (g2[i] < v) v = g2[i];            // pode cair ao necessario
        float floor_v = g[i - 1] - attack_rate;
        if (v < floor_v) v = floor_v;        // queda nao instantanea
        g[i] = v;
    }

    float maxred = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        L[i] *= g[i];
        R[i] *= g[i];
        if (g[i] < 1.0f) {
            float red = -20.0f * std::log10(g[i]);
            if (red > maxred) maxred = red;
        }
    }

    if (out_max_reduction_db) *out_max_reduction_db = maxred;
    return maxred;
}

} // namespace slac::dsp
