#pragma once

#include "block_processor.hpp"
#include <cstdint>
#include "ring_buffer.hpp"
#include "streaming_decoder.hpp"

#include <cstdint>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace slac::rt {

class RealtimePlayer {
public:
    bool open(const std::string& path, const dsp::SpatialChainConfig& cfg,
              size_t block_size = 128) {
        if (!StreamingDecoder::open(path, decoder_)) return false;
        cfg_ = cfg;
        block_size_ = block_size;
        
        if (decoder_.embedded_hrir().present()) {
            const auto& e_hrir = decoder_.embedded_hrir();
            embedded_hrir_cache_.sample_rate = e_hrir.sample_rate;
            embedded_hrir_cache_.ll = e_hrir.ll;
            embedded_hrir_cache_.rr = e_hrir.rr;
            if (e_hrir.channels >= 4) {
                embedded_hrir_cache_.lr = e_hrir.lr;
                embedded_hrir_cache_.rl = e_hrir.rl;
            }
            cfg_.hrir = &embedded_hrir_cache_;
        }

        processor_.configure(cfg_, decoder_.sample_rate(), block_size);

        size_t ring_capacity = 65536;
        ring_L_ = std::make_unique<RingBuffer<float>>(ring_capacity);
        ring_R_ = std::make_unique<RingBuffer<float>>(ring_capacity);

        blockL_.resize(block_size);
        blockR_.resize(block_size);
        procL_.resize(block_size);
        procR_.resize(block_size);

        const int bits = decoder_.bits_per_sample();
        scale_ = 1.0f / static_cast<float>(1 << (bits - 1));
        
        return true; // <-- CORREÇÃO: Faltava este return
    }

    bool start() {
        stop_requested_.store(false, std::memory_order_release);
        producer_done_.store(false, std::memory_order_release);
        producer_ = std::thread([this]() { producer_loop(); });
        return true;
    }

    size_t process_block(float* outL, float* outR) {
        size_t avail = std::min(ring_L_->available_to_read(),
                                ring_R_->available_to_read());
        size_t to_read = std::min(avail, block_size_);

        if (to_read == 0) {
            std::fill(outL, outL + block_size_, 0.0f);
            std::fill(outR, outR + block_size_, 0.0f);
            return 0;
        }

        ring_L_->read(blockL_.data(), to_read);
        ring_R_->read(blockR_.data(), to_read);

        for (size_t i = to_read; i < block_size_; ++i) {
            blockL_[i] = 0.0f;
            blockR_[i] = 0.0f;
        }

        processor_.process_block(blockL_.data(), blockR_.data(),
                                 procL_.data(), procR_.data());

        std::copy(procL_.begin(), procL_.begin() + to_read, outL);
        std::copy(procR_.begin(), procR_.begin() + to_read, outR);
        return to_read;
    }

    // <-- CORREÇÃO: Método render_all restaurado para os testes smoke
    std::vector<std::vector<float>> render_all() {
        start();
        std::vector<std::vector<float>> output(2);
        while (true) {
            size_t n = process_block(procL_.data(), procR_.data());
            if (n == 0) {
                if (finished()) break;
                std::this_thread::yield();
                continue;
            }
            output[0].insert(output[0].end(), procL_.begin(), procL_.begin() + n);
            output[1].insert(output[1].end(), procR_.begin(), procR_.begin() + n);
        }
        stop();
        return output;
    }

    void stop() {
        stop_requested_.store(true, std::memory_order_release);
        if (producer_.joinable()) producer_.join();
    }

    bool finished() const {
        return producer_done_.load(std::memory_order_acquire) &&
               ring_L_->available_to_read() == 0 &&
               ring_R_->available_to_read() == 0;
    }

    uint32_t sample_rate() const { return decoder_.sample_rate(); }
    uint8_t channels() const { return decoder_.channels(); }
    uint64_t total_samples() const { return decoder_.total_samples(); }
    size_t block_size() const { return block_size_; }

    const slac::SpatMetadata& spat() const { return decoder_.spat(); }
    const std::vector<slac::core::AutoKeyframe>& auto_keyframes() const {
        return decoder_.auto_keyframes();
    }

    void reconfigure(const dsp::SpatialChainConfig& cfg) {
    cfg_ = cfg;
    processor_.configure(cfg, decoder_.sample_rate(), block_size_);
    }

    // NOVO: Permite sobrescrever a HRIR (ex: via JNI)
    void setExternalHrir(const slac::dsp::TrueStereoIR& ir) {
        external_hrir_cache_ = ir;
        cfg_.hrir = &external_hrir_cache_;
        cfg_.hrir_unity_gain = true; // Boa prática para HRIRs externas
        processor_.configure(cfg_, decoder_.sample_rate(), block_size_);
    } 

    bool seekToSample(uint64_t target_sample) {
        bool success = decoder_.seek_to_sample(target_sample);
        if (success) {
            if (ring_L_) ring_L_->clear();
            if (ring_R_) ring_R_->clear();
        }
        return success;
    }

    uint64_t current_sample() const {
        return decoder_.position();
    }

    const StreamingDecoder& decoder() const { return decoder_; }

private:
    void producer_loop() {
        std::vector<std::vector<int32_t>> pcm_chunk;
        const size_t chunk_size = 4096;

        while (!stop_requested_.load(std::memory_order_acquire)) {
            size_t n = decoder_.read(pcm_chunk, chunk_size);
            if (n == 0) break;

            size_t offset = 0;
            while (offset < n && !stop_requested_.load(std::memory_order_acquire)) {
                size_t space = std::min(ring_L_->available_to_write(),
                                        ring_R_->available_to_write());
                size_t to_write = std::min(space, n - offset);

                if (to_write > 0) {
                    std::vector<float> fL(to_write), fR(to_write);
                    for (size_t i = 0; i < to_write; ++i) {
                        fL[i] = static_cast<float>(pcm_chunk[0][offset + i]) * scale_;
                        fR[i] = static_cast<float>(pcm_chunk[1][offset + i]) * scale_;
                    }
                    ring_L_->write(fL.data(), to_write);
                    ring_R_->write(fR.data(), to_write);
                    offset += to_write;
                } else {
                    std::this_thread::yield();
                }
            }
        }
        producer_done_.store(true, std::memory_order_release);
    }

    StreamingDecoder decoder_;
    dsp::SpatialChainConfig cfg_;
    BlockProcessor processor_;
    std::unique_ptr<RingBuffer<float>> ring_L_;
    std::unique_ptr<RingBuffer<float>> ring_R_;
    size_t block_size_ = 128;
    float scale_ = 1.0f;

    slac::dsp::TrueStereoIR embedded_hrir_cache_;
    slac::dsp::TrueStereoIR external_hrir_cache_;

    std::thread producer_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> producer_done_{false};

    std::vector<float> blockL_, blockR_, procL_, procR_;
};

} // namespace slac::rt