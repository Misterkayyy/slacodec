#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "rice.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace slac {

// ──────────────────────────────────────────────────────────────
// Quantized LPC coefficients
// ──────────────────────────────────────────────────────────────

struct QuantizedLpc {
    int order = 0;
    int shift = 0;
    std::vector<int32_t> coeffs;
};

// ──────────────────────────────────────────────────────────────
// Autocorrelation (int32 input, original API kept for compat)
// ──────────────────────────────────────────────────────────────

inline std::vector<double> autocorrelation(
    const std::vector<int32_t>& samples,
    int maxOrder
) {
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

// ──────────────────────────────────────────────────────────────
// Autocorrelation from double buffer (windowed signal)
// ──────────────────────────────────────────────────────────────

inline std::vector<double> autocorrelationDouble(
    const std::vector<double>& samples,
    int maxOrder
) {
    int n = static_cast<int>(samples.size());
    int order = std::min(maxOrder, n > 0 ? n - 1 : 0);

    std::vector<double> r(order + 1, 0.0);

    for (int lag = 0; lag <= order; ++lag) {
        double sum = 0.0;
        for (int i = 0; i < n - lag; ++i) {
            sum += samples[i] * samples[i + lag];
        }
        r[lag] = sum;
    }

    return r;
}

// ──────────────────────────────────────────────────────────────
// Levinson-Durbin (single order, original API kept for compat)
// ──────────────────────────────────────────────────────────────

inline std::vector<double> levinsonDurbin(
    const std::vector<double>& r,
    int order
) {
    std::vector<double> a(order + 1, 0.0);
    a[0] = 1.0;

    if (r.empty() || order <= 0 || std::abs(r[0]) < 1e-12) {
        return a;
    }

    double e = r[0];
    int maxOrder = std::min(order, static_cast<int>(r.size()) - 1);

    for (int m = 1; m <= maxOrder; ++m) {
        double num = -r[m];
        for (int i = 1; i < m; ++i) {
            num -= a[i] * r[m - i];
        }

        if (std::abs(e) < 1e-12) break;

        double k = num / e;
        if (!std::isfinite(k)) break;

        std::vector<double> old = a;
        for (int i = 1; i < m; ++i) {
            a[i] = old[i] + k * old[m - i];
        }
        a[m] = k;
        e *= (1.0 - k * k);

        if (e < 1e-12) break;
    }

    return a;
}

// ──────────────────────────────────────────────────────────────
// Levinson-Durbin returning ALL intermediate orders (§3.3)
// ──────────────────────────────────────────────────────────────
//
// Returns a vector of size (maxOrder + 1).
// result[m] = coefficient vector for order m (size m+1, result[m][0]=1).

inline std::vector<std::vector<double>> levinsonDurbinAllOrders(
    const std::vector<double>& r,
    int maxOrder
) {
    std::vector<std::vector<double>> results(maxOrder + 1);

    // Order 0: just {1.0}.
    results[0] = {1.0};

    if (r.empty() || maxOrder <= 0 || std::abs(r[0]) < 1e-12) {
        for (int m = 1; m <= maxOrder; ++m) {
            results[m].assign(m + 1, 0.0);
            results[m][0] = 1.0;
        }
        return results;
    }

    std::vector<double> a(maxOrder + 1, 0.0);
    a[0] = 1.0;
    double e = r[0];

    int limit = std::min(maxOrder, static_cast<int>(r.size()) - 1);

    for (int m = 1; m <= limit; ++m) {
        double num = -r[m];
        for (int i = 1; i < m; ++i) {
            num -= a[i] * r[m - i];
        }

        if (std::abs(e) < 1e-12) break;

        double k = num / e;
        if (!std::isfinite(k)) break;

        std::vector<double> old = a;
        for (int i = 1; i < m; ++i) {
            a[i] = old[i] + k * old[m - i];
        }
        a[m] = k;
        e *= (1.0 - k * k);

        // Save this order.
        results[m].assign(a.begin(), a.begin() + m + 1);

        if (e < 1e-12) break;
    }

    // Fill any orders we didn't reach.
    for (int m = 1; m <= maxOrder; ++m) {
        if (results[m].empty()) {
            results[m].assign(m + 1, 0.0);
            results[m][0] = 1.0;
        }
    }

    return results;
}

// ──────────────────────────────────────────────────────────────
// Quantization (original API kept)
// ──────────────────────────────────────────────────────────────

inline QuantizedLpc quantizeLpc(
    const std::vector<double>& a,
    int order,
    int shift
) {
    QuantizedLpc q;
    q.order = order;
    q.shift = shift;
    q.coeffs.assign(order, 0);

    if (order <= 0) return q;

    const double scale = static_cast<double>(1LL << shift);

    for (int i = 0; i < order; ++i) {
        double v = 0.0;

        if (i + 1 < static_cast<int>(a.size())) {
            v = -a[i + 1] * scale;
        }

        if (v > 32767.0) v = 32767.0;
        if (v < -32768.0) v = -32768.0;

        q.coeffs[i] = static_cast<int32_t>(std::llround(v));
    }

    return q;
}

// ──────────────────────────────────────────────────────────────
// Prediction (original API kept)
// ──────────────────────────────────────────────────────────────

inline int32_t predictSample(
    const std::vector<int32_t>& x,
    size_t n,
    const QuantizedLpc& q
) {
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

// ──────────────────────────────────────────────────────────────
// analyzeLpc (original API kept for backward compat)
// ──────────────────────────────────────────────────────────────

inline QuantizedLpc analyzeLpc(
    const std::vector<int32_t>& samples,
    int order,
    int shift
) {
    if (order <= 0 || samples.empty()) {
        return QuantizedLpc{};
    }

    std::vector<double> r = autocorrelation(samples, order);
    std::vector<double> a = levinsonDurbin(r, order);
    return quantizeLpc(a, order, shift);
}

// ──────────────────────────────────────────────────────────────
// Window functions (§3.3 — Welch/Tukey)
//
// Applied ONLY to the autocorrelation analysis.
// The actual LPC prediction always uses original samples.
// ──────────────────────────────────────────────────────────────

enum class WindowType : int {
    Rectangular = 0,
    Welch       = 1,
    Tukey       = 2,
};

inline void applyWindow(
    const std::vector<int32_t>& samples,
    WindowType type,
    double tukeyAlpha,
    std::vector<double>& out
) {
    size_t N = samples.size();
    out.resize(N);

    if (N == 0) return;

    if (type == WindowType::Rectangular) {
        for (size_t i = 0; i < N; ++i) {
            out[i] = static_cast<double>(samples[i]);
        }
        return;
    }

    double nMinus1 = static_cast<double>(N - 1);

    if (type == WindowType::Welch) {
        // Welch: parabolic
        // w[i] = 1 - ((i - M) / M)^2,  M = (N-1)/2
        double M = nMinus1 / 2.0;
        if (M < 1e-12) {
            out[0] = static_cast<double>(samples[0]);
            return;
        }
        for (size_t i = 0; i < N; ++i) {
            double t = (static_cast<double>(i) - M) / M;
            double w = 1.0 - t * t;
            out[i] = static_cast<double>(samples[i]) * w;
        }
        return;
    }

    if (type == WindowType::Tukey) {
        // Tukey (tapered cosine), alpha in [0, 1].
        // alpha=0 → rectangular, alpha=1 → Hann.
        double alpha = std::max(0.0, std::min(tukeyAlpha, 1.0));

        if (alpha <= 0.0 || N < 2) {
            for (size_t i = 0; i < N; ++i) {
                out[i] = static_cast<double>(samples[i]);
            }
            return;
        }

        double edgeLen = alpha * nMinus1 / 2.0;
        if (edgeLen < 1e-12) {
            for (size_t i = 0; i < N; ++i) {
                out[i] = static_cast<double>(samples[i]);
            }
            return;
        }

        for (size_t i = 0; i < N; ++i) {
            double x = static_cast<double>(i);
            double w = 1.0;

            if (x < edgeLen) {
                // Rising taper.
                w = 0.5 * (1.0 + std::cos(M_PI * (x / edgeLen - 1.0)));
            } else if (x > nMinus1 - edgeLen) {
                // Falling taper.
                w = 0.5 * (1.0 + std::cos(M_PI * ((x - nMinus1) / edgeLen + 1.0)));
            }
            // else: w = 1.0 (flat top)

            out[i] = static_cast<double>(samples[i]) * w;
        }
        return;
    }
}

// ──────────────────────────────────────────────────────────────
// Rice bits estimation (for order selection cost model)
// ──────────────────────────────────────────────────────────────

inline uint64_t estimateRiceBits(const int32_t* data, size_t count) {
    if (count == 0) return 0;

    int bestK = 0;
    uint64_t bestCost = std::numeric_limits<uint64_t>::max();

    for (int k = 0; k <= 15; ++k) {
        uint64_t cost = 0;
        for (size_t i = 0; i < count; ++i) {
            uint32_t u = foldSigned(data[i]);
            uint32_t q = u >> k;
            if (q >= kRiceEscapeZeroCount) {
                cost += kRiceEscapeZeroCount + 1 + 32;
            } else {
                cost += q + 1 + static_cast<uint64_t>(k);
            }
            if (cost >= bestCost) break;
        }
        if (cost < bestCost) {
            bestCost = cost;
            bestK = k;
        }
    }

    (void)bestK; // used only for cost computation
    return bestCost;
}

// ──────────────────────────────────────────────────────────────
// Best-order LPC analysis (§3.3 — rate-distortion selection)
// ──────────────────────────────────────────────────────────────

struct LpcBestResult {
    int order = 0;
    QuantizedLpc lpc;
    std::vector<int32_t> residuals;
    uint64_t estimatedBits = std::numeric_limits<uint64_t>::max();
};

// Try multiple LPC orders and return the one with lowest estimated cost.
//
// Window is applied ONLY to the autocorrelation analysis.
// Prediction always uses original (unwindowed) samples.
inline LpcBestResult analyzeLpcBestOrder(
    const std::vector<int32_t>& samples,
    int maxOrder,
    int lpcShift,
    int coeffBits,
    WindowType windowType = WindowType::Tukey,
    double tukeyAlpha = 0.5
) {
    size_t n = samples.size();

    LpcBestResult best;

    if (n == 0) return best;

    maxOrder = std::min(maxOrder, static_cast<int>(n) - 1);
    if (maxOrder < 0) maxOrder = 0;

    // 1. Window for analysis.
    std::vector<double> windowed;
    applyWindow(samples, windowType, tukeyAlpha, windowed);

    // 2. Autocorrelation (on windowed signal).
    std::vector<double> r = autocorrelationDouble(windowed, maxOrder);

    // 3. Levinson-Durbin all orders.
    auto allCoeffs = levinsonDurbinAllOrders(r, maxOrder);

    // 4. Evaluate each candidate order.
    for (int order = 0; order <= maxOrder; ++order) {
        // Quantize.
        QuantizedLpc q = quantizeLpc(allCoeffs[order], order, lpcShift);

        // Compute residuals on ORIGINAL samples.
        std::vector<int32_t> residuals;
        if (order > 0) {
            residuals.resize(n - static_cast<size_t>(order));
            for (size_t i = static_cast<size_t>(order); i < n; ++i) {
                int32_t pred = predictSample(samples, i, q);
                int64_t r64 = static_cast<int64_t>(samples[i])
                            - static_cast<int64_t>(pred);
                residuals[i - order] = static_cast<int32_t>(r64);
            }
        } else {
            residuals = samples;
        }

        // Cost: coefficient bits + residual bits.
        uint64_t coeffBitsTotal = static_cast<uint64_t>(order) * coeffBits;
        uint64_t residualBits   = estimateRiceBits(residuals.data(), residuals.size());
        uint64_t totalBits      = coeffBitsTotal + residualBits;

        if (totalBits < best.estimatedBits) {
            best.estimatedBits = totalBits;
            best.order         = order;
            best.lpc           = q;
            best.residuals     = std::move(residuals);
        }
    }

    return best;
}

} // namespace slac
