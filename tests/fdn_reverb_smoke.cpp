#include <slac/dsp/fdn_reverb.hpp>

#include <cmath>
#include <iostream>
#include <vector>

using namespace slac::dsp;

static bool check(bool cond, const char* label) {
    if (cond) std::cout << "[OK] " << label << "\n";
    else      std::cerr << "[FAIL] " << label << "\n";
    return cond;
}

static double rms(const std::vector<float>& v, size_t a, size_t b) {
    double s = 0.0;
    for (size_t i = a; i < b && i < v.size(); ++i) s += (double)v[i] * v[i];
    return std::sqrt(s / (double)(b - a));
}

int main() {
    bool ok = true;
    const uint32_t sr = 48000;

    // ── 1. Impulso decai (RT60 respeitado) ──────────────────
    {
        FdnReverbParams p = reverb_params_from_descriptors(1, 1, 2, 100.0f); // room/peq/curto
        p.adaptive = false;
        FdnReverb rev;
        rev.configure(p, sr);

        size_t n = sr; // 1 s
        std::vector<float> inL(n, 0.0f), inR(n, 0.0f), oL(n), oR(n);
        inL[0] = 0.5f;
        inR[0] = 0.5f;
        rev.process(inL.data(), inR.data(), oL.data(), oR.data(), n);

        double early = rms(oL, 100, 4000);
        double late  = rms(oL, 44000, 48000);
        ok &= check(early > 1e-6, "impulse produces wet signal");
        ok &= check(late < early * 0.05, "tail decays (RT60 respected)");
    }

    // ── 2. Estabilidade com ruido full-scale ────────────────
    {
        FdnReverbParams p = reverb_params_from_descriptors(3, 3, 4, 100.0f); // hall/grande/longo
        p.adaptive = false;
        FdnReverb rev;
        rev.configure(p, sr);

        size_t n = sr * 2;
        std::vector<float> inL(n), inR(n), oL(n), oR(n);
        uint32_t seed = 777;
        float peak = 0.0f;
        bool finite = true;
        for (size_t i = 0; i < n; ++i) {
            seed = seed * 1103515245u + 12345u;
            inL[i] = (static_cast<float>(seed >> 16) - 16384.0f) / 16384.0f;
            seed = seed * 1103515245u + 12345u;
            inR[i] = (static_cast<float>(seed >> 16) - 16384.0f) / 16384.0f;
        }
        rev.process(inL.data(), inR.data(), oL.data(), oR.data(), n);
        for (size_t i = 0; i < n; ++i) {
            if (!std::isfinite(oL[i]) || !std::isfinite(oR[i])) finite = false;
            peak = std::max(peak, std::max(std::fabs(oL[i]), std::fabs(oR[i])));
        }
        ok &= check(finite, "output finite under full-scale noise");
        ok &= check(peak < 4.0f, "output bounded (no feedback runaway)");
    }

    // ── 3. Compatibilidade mono (sem cancelamento) ──────────
    {
        FdnReverbParams p = reverb_params_from_descriptors(2, 2, 3, 100.0f);
        p.adaptive = false;
        FdnReverb rev;
        rev.configure(p, sr);

        size_t n = sr;
        std::vector<float> inL(n), inR(n), oL(n), oR(n);
        for (size_t i = 0; i < n; ++i)
            inL[i] = inR[i] = 0.4f * std::sin(2.0f * 3.14159f * 220.0f * i / sr);
        rev.process(inL.data(), inR.data(), oL.data(), oR.data(), n);

        std::vector<float> sum(n), dif(n);
        for (size_t i = 0; i < n; ++i) { sum[i] = oL[i] + oR[i]; dif[i] = oL[i] - oR[i]; }
        double r_sum = rms(sum, sr / 2, n);
        double r_l   = rms(oL,  sr / 2, n);
        ok &= check(r_sum > 0.3 * r_l, "mono-safe: L+R does not cancel");
    }

    // ── 4. Determinismo ─────────────────────────────────────
    {
        FdnReverbParams p = reverb_params_from_descriptors(4, 2, 3, 60.0f);
        p.adaptive = false;
        size_t n = 20000;
        std::vector<float> inL(n), inR(n), a1(n), a2(n), b1(n), b2(n);
        for (size_t i = 0; i < n; ++i) {
            inL[i] = 0.3f * std::sin(2.0f * 3.14159f * 440.0f * i / sr);
            inR[i] = 0.3f * std::cos(2.0f * 3.14159f * 330.0f * i / sr);
        }
        FdnReverb r1, r2;
        r1.configure(p, sr);
        r2.configure(p, sr);
        r1.process(inL.data(), inR.data(), a1.data(), a2.data(), n);
        r2.process(inL.data(), inR.data(), b1.data(), b2.data(), n);
        ok &= check(a1 == b1 && a2 == b2, "deterministic across runs");
    }

    // ── 5. Wet adaptativo difere do fixo em transientes ─────
    {
        FdnReverbParams p = reverb_params_from_descriptors(1, 2, 3, 50.0f);
        size_t n = 48000;
        std::vector<int32_t> L(n, 0), R(n, 0);
        for (size_t i = 0; i < n; i += 4000)
            for (size_t j = i; j < i + 8 && j < n; ++j) { L[j] = 25000; R[j] = 25000; }

        std::vector<int32_t> La = L, Ra = R, Lf = L, Rf = R;
        FdnReverbParams pa = p; pa.adaptive = true;
        FdnReverbParams pf = p; pf.adaptive = false;
        apply_fdn_reverb_offline(La, Ra, pa, sr, true);
        apply_fdn_reverb_offline(Lf, Rf, pf, sr, false);
        ok &= check(La != Lf, "adaptive wet differs from fixed on transients");
    }

    if (!ok) return 1;
    std::cout << "\n--- FDN Reverb smoke PASSED ---\n";
    return 0;
}
