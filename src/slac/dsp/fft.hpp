#pragma once

#include <cmath>
#include <complex>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace slac::dsp {

// Próxima potência de 2 >= n
inline size_t next_pow2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// FFT radix-2 Cooley-Tukey, in-place.
// O tamanho de x DEVE ser potência de 2.
// inverse=false → FFT forward (e^{-i2π/N})
// inverse=true  → IFFT (e^{+i2π/N}, normalizada por 1/n)
inline void fft_inplace(std::vector<std::complex<float>>& x, bool inverse) {
    size_t n = x.size();
    if (n <= 1) return;

    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    // Cooley-Tukey iterativo
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = 2.0f * static_cast<float>(M_PI) / static_cast<float>(len)
                    * (inverse ? 1.0f : -1.0f);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j) {
                std::complex<float> u = x[i + j];
                std::complex<float> v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        float inv_n = 1.0f / static_cast<float>(n);
        for (auto& val : x) val *= inv_n;
    }
}

// Convolução linear via FFT (offline, O(n log n)).
// Retorna vetor de tamanho a.size() + b.size() - 1.
inline std::vector<float> convolve_fft(const std::vector<float>& a,
                                       const std::vector<float>& b) {
    if (a.empty() || b.empty()) return {};

    size_t out_len = a.size() + b.size() - 1;
    size_t fft_size = next_pow2(out_len);

    std::vector<std::complex<float>> fa(fft_size, {0.0f, 0.0f});
    std::vector<std::complex<float>> fb(fft_size, {0.0f, 0.0f});

    for (size_t i = 0; i < a.size(); ++i) fa[i] = {a[i], 0.0f};
    for (size_t i = 0; i < b.size(); ++i) fb[i] = {b[i], 0.0f};

    fft_inplace(fa, false);
    fft_inplace(fb, false);

    for (size_t i = 0; i < fft_size; ++i) fa[i] *= fb[i];

    fft_inplace(fa, true);

    std::vector<float> out(out_len);
    for (size_t i = 0; i < out_len; ++i) out[i] = fa[i].real();
    return out;
}

} // namespace slac::dsp
