#pragma once

#include "realtime_player.hpp"

// Força API level 26+ (AAudio existe desde Android 8.0 / API 26).
#if defined(__ANDROID__)
#  if !defined(__ANDROID_API__) || __ANDROID_API__ < 26
#    undef __ANDROID_API__
#    define __ANDROID_API__ 26
#  endif
#endif

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace slac::rt {

// Wrapper do AAudioStream com callback de baixa latência.
class AudioEngine {
public:
    ~AudioEngine() { stop(); }

    bool start(uint32_t sample_rate, int channels, size_t block_size,
               RealtimePlayer* player) {
        player_ = player;
        channels_ = channels;
        block_size_ = block_size;

        AAudioStreamBuilder* builder = nullptr;
        aaudio_result_t result = AAudio_createStreamBuilder(&builder);
        if (result != AAUDIO_OK || !builder) {
            std::fprintf(stderr, "AAudio: failed to create builder (%d)\n", result);
            return false;
        }

        AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(sample_rate));
        AAudioStreamBuilder_setChannelCount(builder, channels);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setFramesPerDataCallback(builder, static_cast<int32_t>(block_size));
        AAudioStreamBuilder_setDataCallback(builder, onAudioReady, this);

        result = AAudioStreamBuilder_openStream(builder, &stream_);
        AAudioStreamBuilder_delete(builder);

        if (result != AAUDIO_OK || !stream_) {
            std::fprintf(stderr, "AAudio: failed to open stream (%d)\n", result);
            return false;
        }

        actual_sample_rate_ = static_cast<uint32_t>(AAudioStream_getSampleRate(stream_));
        actual_block_size_ = static_cast<size_t>(
            AAudioStream_getFramesPerDataCallback(stream_));

        // Buffer interleaveado (L,R,L,R,...) usado no callback.
        interleaved_.resize(block_size_ * channels_);

        return true;
    }

    bool play() {
        if (!stream_) return false;
        aaudio_result_t result = AAudioStream_requestStart(stream_);
        if (result != AAUDIO_OK) {
            std::fprintf(stderr, "AAudio: failed to start (%d)\n", result);
            return false;
        }
        running_.store(true, std::memory_order_release);
        return true;
    }

    void stop() {
        if (stream_) {
            running_.store(false, std::memory_order_release);
            AAudioStream_requestStop(stream_);
            AAudioStream_close(stream_);
            stream_ = nullptr;
        }
    }

    bool isRunning() const {
        return running_.load(std::memory_order_acquire) &&
               stream_ != nullptr &&
               AAudioStream_getState(stream_) == AAUDIO_STREAM_STATE_STARTED;
    }

    uint32_t actual_sample_rate() const { return actual_sample_rate_; }
    size_t actual_block_size() const { return actual_block_size_; }

private:
    AAudioStream* stream_ = nullptr;
    RealtimePlayer* player_ = nullptr;
    int channels_ = 2;
    size_t block_size_ = 128;
    uint32_t actual_sample_rate_ = 48000;
    size_t actual_block_size_ = 128;
    std::atomic<bool> running_{false};

    // Buffers temporários para o callback (pré-alocados).
    std::vector<float> interleaved_;
    std::vector<float> cb_L_, cb_R_;

    static aaudio_data_callback_result_t onAudioReady(
        AAudioStream* stream, void* userdata,
        void* audioData, int32_t numFrames)
    {
        auto* engine = static_cast<AudioEngine*>(userdata);
        auto* out = static_cast<float*>(audioData);
        (void)stream;

        // Garante buffers no tamanho certo (primeira chamada).
        if (engine->cb_L_.size() < static_cast<size_t>(numFrames)) {
            engine->cb_L_.resize(numFrames);
            engine->cb_R_.resize(numFrames);
        }

        if (!engine->player_) {
            for (int32_t i = 0; i < numFrames * engine->channels_; ++i) out[i] = 0.0f;
            return AAUDIO_CALLBACK_RESULT_CONTINUE;
        }

	engine->player_->process_block(
            engine->cb_L_.data(), engine->cb_R_.data());

        // Interleava L/R no buffer de saída.
        if (engine->channels_ == 2) {
            for (int32_t i = 0; i < numFrames; ++i) {
                out[i * 2 + 0] = engine->cb_L_[i];
                out[i * 2 + 1] = engine->cb_R_[i];
            }
        } else {
            for (int32_t i = 0; i < numFrames; ++i) {
                out[i] = engine->cb_L_[i];
            }
        }

        // Se o player terminou, sinaliza fim.
        if (engine->player_->finished()) {
            // Continua por mais alguns callbacks para esvaziar, depois para.
            engine->running_.store(false, std::memory_order_release);
        }

        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
};

} // namespace slac::rt
