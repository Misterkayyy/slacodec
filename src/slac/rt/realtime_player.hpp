#pragma once

#include "block_processor.hpp"
#include "ring_buffer.hpp"
#include "streaming_decoder.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace slac::rt {

// ──────────────────────────────────────────────────────────────
// Pipeline completo de decode em tempo real.
//
//   StreamingDecoder ──▶ RingBuffer ──▶ BlockProcessor ──▶ out
//     (thread prod.)     (lock-free)     (thread cons.)
//
// render_all() processa o arquivo inteiro (validação offline).
// Para tempo real, substitua o consumer pelo audio callback (5E).
// ──────────────────────────────────────────────────────────────
class RealtimePlayer {
public:
    bool open(const std::string& path, const dsp::SpatialChainConfig& cfg,
              size_t block_size = 128) {
        if (!StreamingDecoder::open(path, decoder_)) return false;
        cfg_ = cfg;
        block_size_ = block_size;
        processor_.configure(cfg, decoder_.sample_rate(), block_size);

        size_t ring_capacity = 65536;  // ~1.3s a 48kHz.
        ring_L_ = std::make_unique<RingBuffer<float>>(ring_capacity);
        ring_R_ = std::make_unique<RingBuffer<float>>(ring_capacity);
        return true;
    }

    // Processa o arquivo inteiro. Retorna PCM float por canal.
    std::vector<std::vector<float>> render_all() {
        const int bits = decoder_.bits_per_sample();
        const float scale = 1.0f / static_cast<float>(1 << (bits - 1));

        std::atomic<bool> producer_done{false};
        std::vector<std::vector<float>> output(2);

        // ── Thread produtora: decode → ring buffers ──────────
        std::thread producer([&]() {
            std::vector<std::vector<int32_t>> pcm_chunk;
            const size_t chunk_size = 4096;

            while (true) {
                size_t n = decoder_.read(pcm_chunk, chunk_size);
                if (n == 0) break;

                size_t offset = 0;
                while (offset < n) {
                    size_t space = std::min(ring_L_->available_to_write(),
                                            ring_R_->available_to_write());
                    size_t to_write = std::min(space, n - offset);

                    if (to_write > 0) {
                        std::vector<float> fL(to_write), fR(to_write);
                        for (size_t i = 0; i < to_write; ++i) {
                            fL[i] = static_cast<float>(pcm_chunk[0][offset + i]) * scale;
                            fR[i] = static_cast<float>(pcm_chunk[1][offset + i]) * scale;
                        }
                        ring_L_->write(fL.data(), to_write);
                        ring_R_->write(fR.data(), to_write);
                        offset += to_write;
                    } else {
                        std::this_thread::yield();
                    }
                }
            }
            producer_done.store(true, std::memory_order_release);
        });

        // ── Consumer (main thread): ring buffers → BlockProcessor ──
        std::vector<float> blockL(block_size_), blockR(block_size_);
        std::vector<float> outL(block_size_), outR(block_size_);

        while (true) {
            size_t avail = std::min(ring_L_->available_to_read(),
                                    ring_R_->available_to_read());
            size_t to_read = std::min(avail, block_size_);

            if (to_read == 0) {
                if (producer_done.load(std::memory_order_acquire) &&
                    ring_L_->available_to_read() == 0 &&
                    ring_R_->available_to_read() == 0) {
                    break;
                }
                std::this_thread::yield();
                continue;
            }

            ring_L_->read(blockL.data(), to_read);
            ring_R_->read(blockR.data(), to_read);

            // Zero-pad o restante do bloco.
            for (size_t i = to_read; i < block_size_; ++i) {
                blockL[i] = 0.0f;
                blockR[i] = 0.0f;
            }

            processor_.process_block(blockL.data(), blockR.data(),
                                     outL.data(), outR.data());

            output[0].insert(output[0].end(), outL.begin(), outL.begin() + to_read);
            output[1].insert(output[1].end(), outR.begin(), outR.begin() + to_read);
        }

        producer.join();
        return output;
    }

    uint32_t sample_rate() const { return decoder_.sample_rate(); }
    uint8_t channels() const { return decoder_.channels(); }
    uint64_t total_samples() const { return decoder_.total_samples(); }

private:
    StreamingDecoder decoder_;
    dsp::SpatialChainConfig cfg_;
    BlockProcessor processor_;
    std::unique_ptr<RingBuffer<float>> ring_L_;
    std::unique_ptr<RingBuffer<float>> ring_R_;
    size_t block_size_ = 128;
};

} // namespace slac::rt
