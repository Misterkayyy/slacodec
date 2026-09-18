#include "slac/dsp/fft.hpp"
#include "slac/dsp/partitioned_convolver.hpp"
#include "slac/dsp/spatial_chain.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace slac::dsp;

static std::vector<float> convolve_direct(const std::vector<float>& a,
                                          const std::vector<float>& b) {
    if (a.empty() || b.empty()) return {};
    std::vector<float> out(a.size() + b.size() - 1, 0.0f);
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t k = 0; k < b.size(); ++k)
            out[i + k] += a[i] * b[k];
    return out;
}

int main() {
    int failures = 0;

    {
        std::vector<std::complex<float>> x(8);
        for (int i = 0; i < 8; ++i) x[i] = {static_cast<float>(i), 0.0f};
        auto orig = x;
        fft_inplace(x, false);
        fft_inplace(x, true);
        float max_err = 0.0f;
        for (int i = 0; i < 8; ++i)
            max_err = std::max(max_err, std::abs(x[i] - orig[i]));
        std::cout << "[TEST] FFT roundtrip max_err=" << max_err << "\n";
        if (max_err > 1e-3f) { std::cerr << "FAIL: FFT roundtrip\n"; ++failures; }
    }

    {
        std::vector<float> a(1000), b(256);
        for (size_t i = 0; i < a.size(); ++i) a[i] = std::sin(0.01f * i);
        for (size_t i = 0; i < b.size(); ++i)
            b[i] = std::exp(-0.01f * i) * std::sin(0.1f * i);
        auto direct = convolve_direct(a, b);
        auto fft_conv = convolve_fft(a, b);
        float max_err = 0.0f;
        for (size_t i = 0; i < direct.size(); ++i)
            max_err = std::max(max_err, std::abs(direct[i] - fft_conv[i]));
        std::cout << "[TEST] convolve_fft vs direct max_err=" << max_err << "\n";
        if (max_err > 1e-2f) { std::cerr << "FAIL: convolve_fft\n"; ++failures; }
    }

    {
        size_t block_size = 32;
        std::vector<float> ir = {1.0f, 0.5f, 0.25f, 0.125f};
        PartitionedConvolver conv;
        conv.configure(ir, block_size);
        std::vector<float> in_block(block_size, 0.0f);
        std::vector<float> out_block(block_size, 0.0f);
        in_block[0] = 1.0f;
        std::vector<float> response;
        for (int b = 0; b < 4; ++b) {
            conv.process(in_block.data(), out_block.data());
            response.insert(response.end(), out_block.begin(), out_block.end());
            std::fill(in_block.begin(), in_block.end(), 0.0f);
        }
        bool found = false;
        size_t found_at = 0;
        for (size_t start = 0; start + ir.size() <= response.size(); ++start) {
            bool match = true;
            for (size_t i = 0; i < ir.size(); ++i) {
                if (std::abs(response[start + i] - ir[i]) > 1e-3f) {
                    match = false;
                    break;
                }
            }
            if (match) { found = true; found_at = start; break; }
        }
        std::cout << "[TEST] impulse response found=" << (found ? "yes" : "no")
                  << " at=" << found_at << "\n";
        if (!found) { std::cerr << "FAIL: impulse response\n"; ++failures; }
    }

    {
        size_t sig_len = 2000;
        std::vector<float> signal(sig_len);
        for (size_t i = 0; i < sig_len; ++i)
            signal[i] = std::sin(0.02f * i) + 0.5f * std::sin(0.05f * i);
        size_t ir_len = 300;
        std::vector<float> ir(ir_len);
        for (size_t i = 0; i < ir_len; ++i)
            ir[i] = std::exp(-0.005f * i) * std::sin(0.1f * i);
        auto direct = convolve_direct(signal, ir);
        size_t block_size = 64;
        PartitionedConvolver conv;
        conv.configure(ir, block_size);
        size_t num_blocks = (sig_len + block_size - 1) / block_size;
        size_t extra_blocks = conv.num_partitions();
        size_t total_blocks = num_blocks + extra_blocks;
        std::vector<float> partitioned_out;
        partitioned_out.reserve(total_blocks * block_size);
        std::vector<float> in_block(block_size, 0.0f);
        std::vector<float> out_block(block_size, 0.0f);
        for (size_t b = 0; b < total_blocks; ++b) {
            for (size_t i = 0; i < block_size; ++i) {
                size_t idx = b * block_size + i;
                in_block[i] = (idx < sig_len) ? signal[idx] : 0.0f;
            }
            conv.process(in_block.data(), out_block.data());
            partitioned_out.insert(partitioned_out.end(),
                                   out_block.begin(), out_block.end());
        }
        size_t best_offset = 0;
        double best_err = 1e30;
        size_t max_offset = 4 * block_size;
        for (size_t offset = 0; offset < max_offset && offset < partitioned_out.size(); ++offset) {
            double err = 0.0;
            size_t count = 0;
            for (size_t i = 0; i < direct.size() && i + offset < partitioned_out.size(); ++i) {
                double d = static_cast<double>(direct[i]) - partitioned_out[i + offset];
                err += d * d;
                ++count;
            }
            if (count > 0) {
                err /= count;
                if (err < best_err) { best_err = err; best_offset = offset; }
            }
        }
        float rms_err = static_cast<float>(std::sqrt(best_err));
        std::cout << "[TEST] PartitionedConvolver offset=" << best_offset
                  << " rms_err=" << rms_err << "\n";
        if (rms_err > 1e-3f) { std::cerr << "FAIL: PartitionedConvolver\n"; ++failures; }
    }

    {
        TrueStereoIR ir;
        size_t ir_len = 100;
        ir.ll.resize(ir_len); ir.lr.resize(ir_len);
        ir.rl.resize(ir_len); ir.rr.resize(ir_len);
        for (size_t i = 0; i < ir_len; ++i) {
            float decay = std::exp(-0.02f * i);
            ir.ll[i] = decay * std::sin(0.10f * i);
            ir.lr[i] = decay * 0.3f * std::sin(0.15f * i);
            ir.rl[i] = decay * 0.3f * std::sin(0.12f * i);
            ir.rr[i] = decay * std::sin(0.11f * i);
        }
        size_t n = 1500;
        std::vector<float> inL(n), inR(n);
        for (size_t i = 0; i < n; ++i) {
            inL[i] = std::sin(0.03f * i);
            inR[i] = std::sin(0.04f * i + 0.5f);
        }
        std::vector<float> directL, directR;
        conv_true_stereo_float(inL, inR, ir, directL, directR);
        std::vector<float> partL, partR;
        conv_true_stereo_partitioned(inL, inR, ir, partL, partR, 64);
        float max_errL = 0.0f, max_errR = 0.0f;
        size_t cmp_len = std::min(directL.size(), partL.size());
        for (size_t i = 0; i < cmp_len; ++i) {
            max_errL = std::max(max_errL, std::abs(directL[i] - partL[i]));
            max_errR = std::max(max_errR, std::abs(directR[i] - partR[i]));
        }
        std::cout << "[TEST] true-stereo partitioned max_errL=" << max_errL
                  << " max_errR=" << max_errR << "\n";
        if (max_errL > 1e-2f || max_errR > 1e-2f) {
            std::cerr << "FAIL: true-stereo partitioned\n";
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "\n=== FFT / PARTITIONED CONV SMOKE TESTS PASSED ===\n";
        return 0;
    }
    std::cerr << "\n=== " << failures << " TEST(S) FAILED ===\n";
    return 1;
}
