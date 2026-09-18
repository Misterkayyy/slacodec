#pragma once

#include "fft.hpp"
#include "convolver.hpp"

#include <algorithm>
#include <complex>
#include <vector>

namespace slac::dsp {

// ──────────────────────────────────────────────────────────────
// Convolução particionada uniforme (streaming / tempo real).
//
// Método: frequency-domain delay line + overlap-save.
// - Latência fixa de block_size samples.
// - Complexidade por bloco: O(P * B log B), P = num_partitions.
// - Para HRIR de 256 taps com block_size=128 → apenas 2 partições.
// ──────────────────────────────────────────────────────────────
class PartitionedConvolver {
public:
    PartitionedConvolver() = default;

    // Configura com um IR (impulse response) e tamanho de bloco.
    void configure(const std::vector<float>& ir, size_t block_size) {
        block_size_ = block_size;
        fft_size_ = 2 * block_size;
        num_partitions_ = (ir.size() + block_size - 1) / block_size;
        if (num_partitions_ == 0) num_partitions_ = 1;

        // FFT de cada partição do IR (zero-padded para fft_size).
        ir_freq_.resize(num_partitions_);
        for (size_t p = 0; p < num_partitions_; ++p) {
            std::vector<std::complex<float>> buf(fft_size_, {0.0f, 0.0f});
            size_t start = p * block_size;
            for (size_t i = 0; i < block_size && start + i < ir.size(); ++i) {
                buf[i] = {ir[start + i], 0.0f};
            }
            fft_inplace(buf, false);
            ir_freq_[p] = std::move(buf);
        }

        // Delay line de espectros de entrada.
        input_delay_line_.assign(num_partitions_,
            std::vector<std::complex<float>>(fft_size_, {0.0f, 0.0f}));
        delay_write_pos_ = 0;

        // Buffers de trabalho.
        input_time_.assign(block_size, 0.0f);
        fft_buf_.resize(fft_size_);
        accum_freq_.resize(fft_size_);
    }

    // Processa um bloco de block_size samples.
    // input e output devem apontar para block_size floats.
    void process(const float* input, float* output) {
        if (block_size_ == 0) return;

        // 1. Montar buffer de 2B: [B anteriores | B novos].
        for (size_t i = 0; i < block_size_; ++i) {
            fft_buf_[i] = {input_time_[i], 0.0f};
            fft_buf_[block_size_ + i] = {input[i], 0.0f};
        }

        // 2. FFT do buffer.
        fft_inplace(fft_buf_, false);

        // 3. Armazenar espectro no delay line.
        input_delay_line_[delay_write_pos_] = fft_buf_;

        // 4. Acumular em frequência: Y = Σ input_delay[p] * ir_freq[p].
        std::fill(accum_freq_.begin(), accum_freq_.end(),
                  std::complex<float>{0.0f, 0.0f});
        for (size_t p = 0; p < num_partitions_; ++p) {
            size_t read_pos = (delay_write_pos_ + num_partitions_ - p) % num_partitions_;
            const auto& in_spec = input_delay_line_[read_pos];
            const auto& ir_spec = ir_freq_[p];
            for (size_t i = 0; i < fft_size_; ++i) {
                accum_freq_[i] += in_spec[i] * ir_spec[i];
            }
        }

        // 5. IFFT.
        fft_inplace(accum_freq_, true);

        // 6. Overlap-save: últimos B samples são a saída válida.
        for (size_t i = 0; i < block_size_; ++i) {
            output[i] = accum_freq_[block_size_ + i].real();
        }

        // 7. Atualizar histórico de entrada.
        std::copy(input, input + block_size_, input_time_.begin());

        // 8. Avançar delay line.
        delay_write_pos_ = (delay_write_pos_ + 1) % num_partitions_;
    }

    // Limpa o estado interno (mantém a configuração).
    void reset() {
        for (auto& spec : input_delay_line_)
            std::fill(spec.begin(), spec.end(), std::complex<float>{0.0f, 0.0f});
        delay_write_pos_ = 0;
        std::fill(input_time_.begin(), input_time_.end(), 0.0f);
    }

    size_t block_size() const { return block_size_; }
    size_t num_partitions() const { return num_partitions_; }
    size_t latency() const { return block_size_; }

private:
    size_t block_size_ = 0;
    size_t fft_size_ = 0;
    size_t num_partitions_ = 0;

    std::vector<std::vector<std::complex<float>>> ir_freq_;
    std::vector<std::vector<std::complex<float>>> input_delay_line_;
    size_t delay_write_pos_ = 0;

    std::vector<float> input_time_;
    std::vector<std::complex<float>> fft_buf_;
    std::vector<std::complex<float>> accum_freq_;
};

// ──────────────────────────────────────────────────────────────
// Convolução true-stereo particionada (4 caminhos do HRIR).
// outL = conv(inL, ll) + conv(inR, rl)
// outR = conv(inL, lr) + conv(inR, rr)
// ──────────────────────────────────────────────────────────────
class TrueStereoPartitionedConvolver {
public:
    void configure(const TrueStereoIR& ir, size_t block_size) {
        block_size_ = block_size;
        conv_ll_.configure(ir.ll, block_size);
        conv_lr_.configure(ir.lr, block_size);
        conv_rl_.configure(ir.rl, block_size);
        conv_rr_.configure(ir.rr, block_size);
        tmp_ll_.resize(block_size);
        tmp_lr_.resize(block_size);
        tmp_rl_.resize(block_size);
        tmp_rr_.resize(block_size);
    }

    // Processa um bloco de block_size samples.
    void process(const float* inL, const float* inR,
                 float* outL, float* outR) {
        conv_ll_.process(inL, tmp_ll_.data());
        conv_rl_.process(inR, tmp_rl_.data());
        conv_lr_.process(inL, tmp_lr_.data());
        conv_rr_.process(inR, tmp_rr_.data());

        for (size_t i = 0; i < block_size_; ++i) {
            outL[i] = tmp_ll_[i] + tmp_rl_[i];
            outR[i] = tmp_lr_[i] + tmp_rr_[i];
        }
    }

    void reset() {
        conv_ll_.reset();
        conv_lr_.reset();
        conv_rl_.reset();
        conv_rr_.reset();
    }

    size_t block_size() const { return block_size_; }
    size_t num_partitions() const { return conv_ll_.num_partitions(); }

private:
    PartitionedConvolver conv_ll_, conv_lr_, conv_rl_, conv_rr_;
    size_t block_size_ = 0;
    std::vector<float> tmp_ll_, tmp_lr_, tmp_rl_, tmp_rr_;
};

// Convolução true-stereo particionada offline (processa o áudio inteiro).
// Equivalente a conv_true_stereo_float, mas O(n log n).
inline void conv_true_stereo_partitioned(
    const std::vector<float>& inL, const std::vector<float>& inR,
    const TrueStereoIR& ir,
    std::vector<float>& outL, std::vector<float>& outR,
    size_t block_size = 128)
{
    size_t n = inL.size();
    if (n == 0) { outL.clear(); outR.clear(); return; }

    size_t ir_len = ir.length();
    size_t out_len = n + ir_len - 1;

    TrueStereoPartitionedConvolver conv;
    conv.configure(ir, block_size);

    size_t num_input_blocks = (n + block_size - 1) / block_size;
    size_t drain_blocks = conv.num_partitions() + 1;
    size_t total_blocks = num_input_blocks + drain_blocks;

    std::vector<float> in_blockL(block_size, 0.0f), in_blockR(block_size, 0.0f);
    std::vector<float> out_blockL(block_size, 0.0f), out_blockR(block_size, 0.0f);

    outL.clear();
    outR.clear();
    outL.reserve(total_blocks * block_size);
    outR.reserve(total_blocks * block_size);

    for (size_t b = 0; b < total_blocks; ++b) {
        for (size_t i = 0; i < block_size; ++i) {
            size_t idx = b * block_size + i;
            in_blockL[i] = (idx < n) ? inL[idx] : 0.0f;
            in_blockR[i] = (idx < n) ? inR[idx] : 0.0f;
        }
        conv.process(in_blockL.data(), in_blockR.data(),
                     out_blockL.data(), out_blockR.data());
        outL.insert(outL.end(), out_blockL.begin(), out_blockL.end());
        outR.insert(outR.end(), out_blockR.begin(), out_blockR.end());
    }

    outL.resize(out_len);
    outR.resize(out_len);
}

} // namespace slac::dsp
