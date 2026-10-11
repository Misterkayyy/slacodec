#pragma once

#include "realtime_player.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <thread>
#include <cstring>
#include <cmath>

// ============================================================================
// Platform-specific includes (MUST be at global scope, BEFORE any namespaces)
// ============================================================================
#if defined(_WIN32)
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <mmdeviceapi.h>
    #include <audioclient.h>
#elif defined(__ANDROID__)
    #if !defined(__ANDROID_API__) || __ANDROID_API__ < 26
    #  undef __ANDROID_API__
    #  define __ANDROID_API__ 26
    #endif
    #include <aaudio/AAudio.h>
#endif

namespace slac::rt {

class AudioEngine {
public:
    ~AudioEngine() { stop(); }

    bool start(uint32_t sample_rate, int channels, size_t block_size,
               RealtimePlayer* player);
    bool play();
    void stop();
    bool isRunning() const;

    uint32_t actual_sample_rate() const { return actual_sample_rate_; }
    size_t actual_block_size() const { return actual_block_size_; }

private:
    RealtimePlayer* player_ = nullptr;
    int channels_ = 2;
    size_t block_size_ = 128;
    uint32_t actual_sample_rate_ = 48000;
    size_t actual_block_size_ = 128;
    std::atomic<bool> running_{false};

    std::vector<float> cb_L_, cb_R_;

#if defined(__ANDROID__)
    AAudioStream* stream_ = nullptr;
    static aaudio_data_callback_result_t onAudioReady(
        AAudioStream* stream, void* userdata,
        void* audioData, int32_t numFrames);

#elif defined(_WIN32)
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    HANDLE buffer_event_ = nullptr;
    std::thread audio_thread_;
    std::atomic<bool> stop_requested_{false};

    // Buffers para resampling (carry-over entre iterações)
    std::vector<float> carry_L_, carry_R_;
    size_t carry_count_ = 0;

    void audio_thread_func();
#endif
};

// ============================================================================
// Implementação: Android (AAudio)
// ============================================================================
#if defined(__ANDROID__)

inline bool AudioEngine::start(uint32_t sample_rate, int channels,
                               size_t block_size, RealtimePlayer* player) {
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
    actual_block_size_ = static_cast<size_t>(AAudioStream_getFramesPerDataCallback(stream_));
    return true;
}

inline bool AudioEngine::play() {
    if (!stream_) return false;
    aaudio_result_t result = AAudioStream_requestStart(stream_);
    if (result != AAUDIO_OK) {
        std::fprintf(stderr, "AAudio: failed to start (%d)\n", result);
        return false;
    }
    running_.store(true, std::memory_order_release);
    return true;
}

inline void AudioEngine::stop() {
    if (stream_) {
        running_.store(false, std::memory_order_release);
        AAudioStream_requestStop(stream_);
        AAudioStream_close(stream_);
        stream_ = nullptr;
    }
}

inline bool AudioEngine::isRunning() const {
    return running_.load(std::memory_order_acquire) &&
           stream_ != nullptr &&
           AAudioStream_getState(stream_) == AAUDIO_STREAM_STATE_STARTED;
}

inline aaudio_data_callback_result_t AudioEngine::onAudioReady(
    AAudioStream* stream, void* userdata, void* audioData, int32_t numFrames)
{
    auto* engine = static_cast<AudioEngine*>(userdata);
    auto* out = static_cast<float*>(audioData);
    (void)stream;

    if (engine->cb_L_.size() < static_cast<size_t>(numFrames)) {
        engine->cb_L_.resize(numFrames);
        engine->cb_R_.resize(numFrames);
    }

    if (!engine->player_) {
        for (int32_t i = 0; i < numFrames * engine->channels_; ++i) out[i] = 0.0f;
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    engine->player_->process_block(engine->cb_L_.data(), engine->cb_R_.data());

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

    if (engine->player_->finished()) {
        engine->running_.store(false, std::memory_order_release);
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// ============================================================================
// Implementação: Windows (WASAPI)
// ============================================================================
#elif defined(_WIN32)

inline bool AudioEngine::start(uint32_t sample_rate, int channels,
                               size_t block_size, RealtimePlayer* player) {
    player_ = player;
    channels_ = channels;
    block_size_ = block_size;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::fprintf(stderr, "WASAPI: CoInitializeEx failed (0x%lx)\n", hr);
        return false;
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator_));
    if (FAILED(hr) || !enumerator_) {
        std::fprintf(stderr, "WASAPI: CoCreateInstance failed (0x%lx)\n", hr);
        return false;
    }

    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr) || !device_) {
        std::fprintf(stderr, "WASAPI: no default audio device (0x%lx)\n", hr);
        return false;
    }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&audio_client_));
    if (FAILED(hr) || !audio_client_) {
        std::fprintf(stderr, "WASAPI: Activate failed (0x%lx)\n", hr);
        return false;
    }

    WAVEFORMATEX* mix_format = nullptr;
    hr = audio_client_->GetMixFormat(&mix_format);
    if (FAILED(hr) || !mix_format) {
        std::fprintf(stderr, "WASAPI: GetMixFormat failed (0x%lx)\n", hr);
        return false;
    }

    bool is_float = (mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                    (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE);
    if (!is_float) {
        std::fprintf(stderr, "WASAPI: mix format is not float32, unsupported\n");
        CoTaskMemFree(mix_format);
        return false;
    }

    REFERENCE_TIME buffer_duration = 1000000; // 100ms

    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                   buffer_duration, 0, mix_format, nullptr);
    if (FAILED(hr)) {
        std::fprintf(stderr, "WASAPI: Initialize failed (0x%lx)\n", hr);
        CoTaskMemFree(mix_format);
        return false;
    }

    hr = audio_client_->GetService(__uuidof(IAudioRenderClient),
                                   reinterpret_cast<void**>(&render_client_));
    if (FAILED(hr) || !render_client_) {
        std::fprintf(stderr, "WASAPI: GetService failed (0x%lx)\n", hr);
        CoTaskMemFree(mix_format);
        return false;
    }

    buffer_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!buffer_event_) {
        std::fprintf(stderr, "WASAPI: CreateEvent failed\n");
        CoTaskMemFree(mix_format);
        return false;
    }

    hr = audio_client_->SetEventHandle(buffer_event_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "WASAPI: SetEventHandle failed (0x%lx)\n", hr);
        CoTaskMemFree(mix_format);
        return false;
    }

    // Adota o formato real do dispositivo
    actual_sample_rate_ = mix_format->nSamplesPerSec;
    channels_ = mix_format->nChannels;

    CoTaskMemFree(mix_format);

    // Preencher o buffer inicial (necessário para event-driven streams)
    UINT32 buffer_frames = 0;
    audio_client_->GetBufferSize(&buffer_frames);
    
    BYTE* initial_data = nullptr;
    hr = render_client_->GetBuffer(buffer_frames, &initial_data);
    if (SUCCEEDED(hr) && initial_data) {
        std::memset(initial_data, 0, buffer_frames * channels_ * sizeof(float));
        render_client_->ReleaseBuffer(buffer_frames, 0);
    }

    return true;
}

inline bool AudioEngine::play() {
    if (!audio_client_) return false;

    stop_requested_.store(false, std::memory_order_release);

    // Sinalizar o evento para acordar o motor de áudio pela primeira vez
    SetEvent(buffer_event_);

    HRESULT hr = audio_client_->Start();
    if (FAILED(hr)) {
        std::fprintf(stderr, "WASAPI: Start failed (0x%lx)\n", hr);
        return false;
    }

    running_.store(true, std::memory_order_release);
    audio_thread_ = std::thread([this]() { audio_thread_func(); });

    return true;
}

inline void AudioEngine::stop() {
    stop_requested_.store(true, std::memory_order_release);

    if (audio_thread_.joinable()) {
        audio_thread_.join();
    }

    if (audio_client_) {
        audio_client_->Stop();
    }

    if (render_client_) { render_client_->Release(); render_client_ = nullptr; }
    if (audio_client_) { audio_client_->Release(); audio_client_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    if (enumerator_) { enumerator_->Release(); enumerator_ = nullptr; }
    if (buffer_event_) { CloseHandle(buffer_event_); buffer_event_ = nullptr; }

    running_.store(false, std::memory_order_release);
    CoUninitialize();
}

inline bool AudioEngine::isRunning() const {
    return running_.load(std::memory_order_acquire);
}

inline void AudioEngine::audio_thread_func() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool com_initialized = SUCCEEDED(hr);

    uint32_t device_rate = actual_sample_rate_;
    uint32_t file_rate = player_ ? player_->sample_rate() : device_rate;
    bool needs_resample = (device_rate != file_rate);
    double resample_ratio = static_cast<double>(file_rate) / static_cast<double>(device_rate);

    while (!stop_requested_.load(std::memory_order_acquire)) {
        DWORD wait_result = WaitForSingleObject(buffer_event_, 1000);

        if (wait_result == WAIT_OBJECT_0) {
            UINT32 padding = 0;
            audio_client_->GetCurrentPadding(&padding);

            UINT32 buffer_size = 0;
            audio_client_->GetBufferSize(&buffer_size);
            UINT32 frames_to_write = buffer_size - padding;

            if (frames_to_write > 0) {
                BYTE* data = nullptr;
                HRESULT hr_render = render_client_->GetBuffer(frames_to_write, &data);

                if (SUCCEEDED(hr_render) && data) {
                    auto* out = reinterpret_cast<float*>(data);

                    if (needs_resample) {
                        UINT32 src_frames_needed = static_cast<UINT32>(
                            std::ceil(frames_to_write * resample_ratio) + 4
                        );

                        size_t from_carry = std::min(static_cast<size_t>(src_frames_needed), carry_count_);
                        
                        if (from_carry > 0) {
                            if (cb_L_.size() < src_frames_needed) {
                                cb_L_.resize(src_frames_needed);
                                cb_R_.resize(src_frames_needed);
                            }
                            std::copy(carry_L_.begin(), carry_L_.begin() + from_carry, cb_L_.begin());
                            std::copy(carry_R_.begin(), carry_R_.begin() + from_carry, cb_R_.begin());
                            carry_count_ -= from_carry;
                            if (carry_count_ > 0) {
                                std::copy(carry_L_.begin() + from_carry, carry_L_.begin() + from_carry + carry_count_, carry_L_.begin());
                                std::copy(carry_R_.begin() + from_carry, carry_R_.begin() + from_carry + carry_count_, carry_R_.begin());
                            }
                        }

                        size_t total_read = from_carry;
                        while (total_read < src_frames_needed && player_ && !player_->finished()) {
                            if (cb_L_.size() < total_read + block_size_) {
                                cb_L_.resize(total_read + block_size_);
                                cb_R_.resize(total_read + block_size_);
                            }
                            size_t read = player_->process_block(
                                cb_L_.data() + total_read,
                                cb_R_.data() + total_read
                            );
                            total_read += read;
                            if (read == 0) break;
                        }

                        for (UINT32 i = 0; i < frames_to_write; ++i) {
                            double src_pos = i * resample_ratio;
                            UINT32 src_idx = static_cast<UINT32>(src_pos);
                            float frac = static_cast<float>(src_pos - src_idx);

                            if (src_idx + 1 < total_read) {
                                float L = cb_L_[src_idx] * (1.0f - frac) + cb_L_[src_idx + 1] * frac;
                                float R = cb_R_[src_idx] * (1.0f - frac) + cb_R_[src_idx + 1] * frac;

                                if (channels_ == 2) {
                                    out[i * 2 + 0] = L;
                                    out[i * 2 + 1] = R;
                                } else {
                                    out[i] = L;
                                }
                            } else if (src_idx < total_read) {
                                if (channels_ == 2) {
                                    out[i * 2 + 0] = cb_L_[src_idx];
                                    out[i * 2 + 1] = cb_R_[src_idx];
                                } else {
                                    out[i] = cb_L_[src_idx];
                                }
                            } else {
                                if (channels_ == 2) {
                                    out[i * 2 + 0] = 0.0f;
                                    out[i * 2 + 1] = 0.0f;
                                } else {
                                    out[i] = 0.0f;
                                }
                            }
                        }

                        size_t last_src_pos = static_cast<size_t>((frames_to_write - 1) * resample_ratio) + 2;
                        if (last_src_pos < total_read) {
                            size_t remaining = total_read - last_src_pos;
                            if (carry_L_.size() < remaining) {
                                carry_L_.resize(remaining);
                                carry_R_.resize(remaining);
                            }
                            std::copy(cb_L_.begin() + last_src_pos, cb_L_.begin() + total_read, carry_L_.begin());
                            std::copy(cb_R_.begin() + last_src_pos, cb_R_.begin() + total_read, carry_R_.begin());
                            carry_count_ = remaining;
                        } else {
                            carry_count_ = 0;
                        }
                    } else {
                        UINT32 frames_filled = 0;

                        while (frames_filled < frames_to_write) {
                            UINT32 frames_this_iter = std::min(
                                static_cast<UINT32>(block_size_),
                                frames_to_write - frames_filled
                            );

                            if (cb_L_.size() < frames_this_iter) {
                                cb_L_.resize(frames_this_iter);
                                cb_R_.resize(frames_this_iter);
                            }

                            if (player_) {
                                size_t processed = player_->process_block(
                                    cb_L_.data(), cb_R_.data()
                                );

                                if (channels_ == 2) {
                                    for (UINT32 i = 0; i < frames_this_iter; ++i) {
                                        if (i < processed) {
                                            out[(frames_filled + i) * 2 + 0] = cb_L_[i];
                                            out[(frames_filled + i) * 2 + 1] = cb_R_[i];
                                        } else {
                                            out[(frames_filled + i) * 2 + 0] = 0.0f;
                                            out[(frames_filled + i) * 2 + 1] = 0.0f;
                                        }
                                    }
                                } else {
                                    for (UINT32 i = 0; i < frames_this_iter; ++i) {
                                        out[frames_filled + i] = (i < processed) ? cb_L_[i] : 0.0f;
                                    }
                                }

                                frames_filled += frames_this_iter;

                                if (player_->finished() && processed == 0) {
                                    for (UINT32 i = frames_filled; i < frames_to_write; ++i) {
                                        if (channels_ == 2) {
                                            out[i * 2 + 0] = 0.0f;
                                            out[i * 2 + 1] = 0.0f;
                                        } else {
                                            out[i] = 0.0f;
                                        }
                                    }
                                    break;
                                }
                            } else {
                                std::memset(data, 0, frames_to_write * channels_ * sizeof(float));
                                break;
                            }
                        }
                    }

                    render_client_->ReleaseBuffer(frames_to_write, 0);
                }
            }
        } else if (wait_result == WAIT_TIMEOUT) {
            if (player_ && player_->finished()) {
                break;
            }
        }
    }

    if (com_initialized) {
        CoUninitialize();
    }
}

#endif

} // namespace slac::rt