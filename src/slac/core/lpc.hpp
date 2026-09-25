#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// NEON SIMD para ARM64 (autocorrelação e prediction otimizados)
#if defined(__ARM_NEON) && defined(__aarch64__)
#include <arm_neon.h>
#define SLAC_HAS_NEON 1
#else
#define SLAC_HAS_NEON 0
#endif

#include "rice.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace slac {

struct QuantizedLpc {
    int order = 0;
    int shift = 0;
    std::vector<int32_t> coeffs;
};

inline int choosePartitionOrder(size_t residualCount) {
    if (residualCount == 0) return 0;
    int order = 0;
    for (int o = 4; o >= 0; --o) {
        size_t numPartitions = static_cast<size_t>(1) << o;
        if (residualCount / numPartitions >= 16) {
            order = o;
            break;
        }
    }
    return order;
}

inline std::vector<double> autocorrelation(
    const std::vector<int32_t>& samples, int maxOrder) {
    int n = static_cast<int>(samples.size());
    int order = std::min(maxOrder, n > 0 ? n - 1 : 0);
    std::vector<double> r(order + 1, 0.0);
    for (int lag = 0; lag <= order; ++lag) {
        double sum = 0.0;
        for (int i = 0; i < n - lag; ++i) {
            double a = static_cast<double>(samples[i]);
            double b = static_cast<double>(samples[i + lag]);
            sum += a * b;
        }
        r[lag] = sum;
    }
    return r;
}

inline std::vector<double> autocorrelationDouble(
    const std::vector<double>& samples, int maxOrder) {
    int n = static_cast<int>(samples.size());
    int order = std::min(maxOrder, n > 0 ? n - 1 : 0);
    std::vector<double> r(order + 1, 0.0);

#if SLAC_HAS_NEON
    // ── Caminho NEON (AArch64): processa 2 doubles por iteração + FMA ──
    const double* data = samples.data();
    for (int lag = 0; lag <= order; ++lag) {
        const int limit = n - lag;
        const double* a_ptr = data;
        const double* b_ptr = data + lag;
        
        // Vetor acumulador (2 lanes de double)
        float64x2_t sum_vec = vdupq_n_f64(0.0);
        
        int i = 0;
        // Loop principal: 2 amostras por vez
        for (; i + 1 < limit; i += 2) {
            float64x2_t a = vld1q_f64(a_ptr + i);
            float64x2_t b = vld1q_f64(b_ptr + i);
            sum_vec = vfmaq_f64(sum_vec, a, b);  // fused multiply-add
        }
        
        // Reduzir vetor para escalar (horizontal sum)
        double sum = vaddvq_f64(sum_vec);
        
        // Processar amostra residual (se limit é ímpar)
        if (i < limit) {
            sum += a_ptr[i] * b_ptr[i];
        }
        
        r[lag] = sum;
    }
#else
    // ── Fallback C++ (plataformas não-NEON) ──
    for (int lag = 0; lag <= order; ++lag) {
        double sum = 0.0;
        for (int i = 0; i < n - lag; ++i) sum += samples[i] * samples[i + lag];
        r[lag] = sum;
    }
#endif

    return r;
}

inline std::vector<double> levinsonDurbin(
    const std::vector<double>& r, int order) {
    std::vector<double> a(order + 1, 0.0);
    a[0] = 1.0;
    if (r.empty() || order <= 0 || std::abs(r[0]) < 1e-12) return a;
    double e = r[0];
    int maxOrder = std::min(order, static_cast<int>(r.size()) - 1);
    for (int m = 1; m <= maxOrder; ++m) {
        double num = -r[m];
        for (int i = 1; i < m; ++i) num -= a[i] * r[m - i];
        if (std::abs(e) < 1e-12) break;
        double k = num / e;
        if (!std::isfinite(k)) break;
        std::vector<double> old = a;
        for (int i = 1; i < m; ++i) a[i] = old[i] + k * old[m - i];
        a[m] = k;
        e *= (1.0 - k * k);
        if (e < 1e-12) break;
    }
    return a;
}

inline std::vector<std::vector<double>> levinsonDurbinAllOrders(
    const std::vector<double>& r, int maxOrder) {
    std::vector<std::vector<double>> results(maxOrder + 1);
    results[0] = {1.0};
    if (r.empty() || maxOrder <= 0 || std::abs(r[0]) < 1e-12) {
        for (int m = 1; m <= maxOrder; ++m) {
            results[m].assign(m + 1, 0.0);
            results[m][0] = 1.0;
        }
        return results;
    }
    
    // Buffers pré-alocados (evita alocações dentro do loop)
    std::vector<double> a(maxOrder + 1, 0.0);
    std::vector<double> a_prev(maxOrder + 1, 0.0);  // substitui "old"
    a[0] = 1.0;
    double e = r[0];
    int limit = std::min(maxOrder, static_cast<int>(r.size()) - 1);
    
    for (int m = 1; m <= limit; ++m) {
        double num = -r[m];
        for (int i = 1; i < m; ++i) num -= a[i] * r[m - i];
        if (std::abs(e) < 1e-12) break;
        double k = num / e;
        if (!std::isfinite(k)) break;
        
        // Swap de buffers (sem alocação)
        for (int i = 1; i < m; ++i) a_prev[i] = a[i];
        for (int i = 1; i < m; ++i) a[i] = a_prev[i] + k * a_prev[m - i];
        a[m] = k;
        e *= (1.0 - k * k);
        
        results[m].assign(a.begin(), a.begin() + m + 1);
        if (e < 1e-12) break;
    }
    return results;
}

inline QuantizedLpc quantizeLpc(
    const std::vector<double>& a, int order, int shift) {
    QuantizedLpc q;
    q.order = order;
    q.shift = shift;
    q.coeffs.assign(order, 0);
    if (order <= 0) return q;
    const double scale = static_cast<double>(1LL << shift);
    for (int i = 0; i < order; ++i) {
        double v = 0.0;
        if (i + 1 < static_cast<int>(a.size())) v = -a[i + 1] * scale;
        if (v > 32767.0) v = 32767.0;
        if (v < -32768.0) v = -32768.0;
        q.coeffs[i] = static_cast<int32_t>(std::llround(v));
    }
    return q;
}

// Escolhe shift dinâmico baseado na magnitude dos coeficientes (estilo FLAC).
inline int chooseLpcShift(const std::vector<double>& a, int order, int coeffBits) {
    double maxMag = 0.0;
    for (int i = 1; i <= order && i < static_cast<int>(a.size()); ++i) {
        maxMag = std::max(maxMag, std::abs(a[i]));
    }
    if (maxMag < 1e-12) return 15;

    double maxVal = static_cast<double>((1LL << (coeffBits - 1)) - 1);
    if (maxMag >= maxVal) return 0;

    int shift = static_cast<int>(std::floor(std::log2(maxVal / maxMag)));
    return std::max(0, std::min(shift, 30));
}

inline int32_t predictSample(
    const std::vector<int32_t>& x, size_t n, const QuantizedLpc& q) {
    if (q.order <= 0) return 0;
    if (n < static_cast<size_t>(q.order)) return 0;
    int64_t sum = 0;
    for (int i = 0; i < q.order; ++i) {
        sum += static_cast<int64_t>(q.coeffs[i]) *
               static_cast<int64_t>(x[n - 1 - i]);
    }
    int64_t div = 1LL << q.shift;
    return static_cast<int32_t>(sum / div);
}

inline QuantizedLpc analyzeLpc(
    const std::vector<int32_t>& samples, int order, int shift) {
    if (order <= 0 || samples.empty()) return QuantizedLpc{};
    std::vector<double> r = autocorrelation(samples, order);
    std::vector<double> a = levinsonDurbin(r, order);
    return quantizeLpc(a, order, shift);
}

enum class WindowType : int {
    Rectangular = 0,
    Welch       = 1,
    Tukey       = 2,
};

inline void applyWindow(
    const std::vector<int32_t>& samples, WindowType type,
    double tukeyAlpha, std::vector<double>& out) {
    size_t N = samples.size();
    out.resize(N);
    if (N == 0) return;
    if (type == WindowType::Rectangular) {
        for (size_t i = 0; i < N; ++i) out[i] = static_cast<double>(samples[i]);
        return;
    }
    double nMinus1 = static_cast<double>(N - 1);
    if (type == WindowType::Welch) {
        double M = nMinus1 / 2.0;
        if (M < 1e-12) { out[0] = static_cast<double>(samples[0]); return; }
        for (size_t i = 0; i < N; ++i) {
            double t = (static_cast<double>(i) - M) / M;
            double w = 1.0 - t * t;
            out[i] = static_cast<double>(samples[i]) * w;
        }
        return;
    }
    if (type == WindowType::Tukey) {
        double alpha = std::max(0.0, std::min(tukeyAlpha, 1.0));
        if (alpha <= 0.0 || N < 2) {
            for (size_t i = 0; i < N; ++i) out[i] = static_cast<double>(samples[i]);
            return;
        }
        double edgeLen = alpha * nMinus1 / 2.0;
        if (edgeLen < 1e-12) {
            for (size_t i = 0; i < N; ++i) out[i] = static_cast<double>(samples[i]);
            return;
        }
        for (size_t i = 0; i < N; ++i) {
            double x = static_cast<double>(i);
            double w = 1.0;
            if (x < edgeLen) w = 0.5 * (1.0 + std::cos(M_PI * (x / edgeLen - 1.0)));
            else if (x > nMinus1 - edgeLen) w = 0.5 * (1.0 + std::cos(M_PI * ((x - nMinus1) / edgeLen + 1.0)));
            out[i] = static_cast<double>(samples[i]) * w;
        }
        return;
    }
}

inline uint64_t estimateRiceBits(const int32_t* data, size_t count, int partitionOrder = 0) {
    if (count == 0) return 0;
    int numPartitions = 1 << partitionOrder;
    size_t basePartSize = count / numPartitions;
    size_t remainder = count % numPartitions;
    uint64_t totalCost = 0;
    size_t offset = 0;
    for (int p = 0; p < numPartitions; ++p) {
        size_t partSize = basePartSize + (static_cast<size_t>(p) < remainder ? 1 : 0);
        if (partSize == 0) continue;
        totalCost += 8;
        uint64_t bestPartCost = std::numeric_limits<uint64_t>::max();
        for (int k = 0; k <= 15; ++k) {
            uint64_t partCost = 0;
            for (size_t i = 0; i < partSize; ++i) {
                uint32_t u = foldSigned(data[offset + i]);
                uint32_t q = u >> k;
                if (q >= kRiceEscapeZeroCount) partCost += kRiceEscapeZeroCount + 1 + 32;
                else partCost += q + 1 + static_cast<uint64_t>(k);
                if (partCost >= bestPartCost) break;
            }
            if (partCost < bestPartCost) bestPartCost = partCost;
        }
        totalCost += bestPartCost;
        offset += partSize;
    }
    return totalCost;
}

struct LpcBestResult {
    int order = 0;
    int shift = 15;
    QuantizedLpc lpc;
    std::vector<int32_t> residuals;
    uint64_t estimatedBits = std::numeric_limits<uint64_t>::max();
};

inline LpcBestResult analyzeLpcBestOrder(
    const std::vector<int32_t>& samples,
    int maxOrder,
    int lpcShift,
    int coeffBits,
    WindowType windowType = WindowType::Tukey,
    double tukeyAlpha = 0.5,
    int bitsPerSample = 16
) {
    size_t n = samples.size();
    LpcBestResult best;
    best.shift = lpcShift;
    if (n == 0) return best;

    maxOrder = std::min(maxOrder, static_cast<int>(n) - 1);
    if (maxOrder < 0) maxOrder = 0;

    std::vector<double> windowed;
    applyWindow(samples, windowType, tukeyAlpha, windowed);

    std::vector<double> r = autocorrelationDouble(windowed, maxOrder);
    auto allCoeffs = levinsonDurbinAllOrders(r, maxOrder);

    for (int order = 0; order <= maxOrder; ++order) {
        int orderShift = (order > 0)
            ? chooseLpcShift(allCoeffs[order], order, coeffBits)
            : lpcShift;

        QuantizedLpc q = quantizeLpc(allCoeffs[order], order, orderShift);

        std::vector<int32_t> residuals;
        if (order > 0) {
            residuals.resize(n - static_cast<size_t>(order));
            for (size_t i = static_cast<size_t>(order); i < n; ++i) {
                int32_t pred = predictSample(samples, i, q);
                int64_t r64 = static_cast<int64_t>(samples[i]) - static_cast<int64_t>(pred);
                residuals[i - order] = static_cast<int32_t>(r64);
            }
        } else {
            residuals = samples;
        }

        uint64_t blockHeaderBits = 8 * 8;
        uint64_t coeffBitsTotal = static_cast<uint64_t>(order) * coeffBits;
        uint64_t warmupBits = static_cast<uint64_t>(order) * bitsPerSample;
        int partitionOrder = choosePartitionOrder(residuals.size());
        uint64_t residualBits = estimateRiceBits(residuals.data(), residuals.size(), partitionOrder);
        uint64_t totalBits = blockHeaderBits + coeffBitsTotal + warmupBits + residualBits;

        if (totalBits < best.estimatedBits) {
            best.estimatedBits = totalBits;
            best.order         = order;
            best.shift         = orderShift;
            best.lpc           = q;
            best.residuals     = std::move(residuals);
        }
    }

    return best;
}

} // namespace slac
