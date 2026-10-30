#pragma once

#if defined(_WIN32)
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601
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
#elif defined(__linux__)
    #include <alsa/asoundlib.h>
#endif

#include "realtime_player.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <vector>

namespace slac::rt {

class AudioEngine {
public:
    ~AudioEngine() { stop(); }

    bool start(uint32_t sample_rate, int channels, size_t block_size, RealtimePlayer* player);
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
        AAudioStream* stream, void* userdata, void* audioData, int32_t numFrames);

#elif defined(_WIN32)
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    HANDLE buffer_event_ = nullptr;
    std::thread audio_thread_;
    std::atomic<bool> stop_requested_{false};
    void audio_thread_func();

#elif defined(__linux__)
    snd_pcm_t* pcm_handle_ = nullptr;
    std::thread playback_thread_;
    std::atomic<bool> stop_requested_{false};
    void alsa_playback_loop();
#endif
};

// ─────────────────────────────────────────────────────────────
// ANDROID (AAudio)
// ──────────────────────────────────────────────────────────────
#if defined(__ANDROID__)

inline bool AudioEngine::start(uint32_t sample_rate, int channels, size_t block_size, RealtimePlayer* player) {
    player_ = player; channels_ = channels; block_size_ = block_size;
    AAudioStreamBuilder* builder = nullptr;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK || !builder) return false;
    
    AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(sample_rate));
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFramesPerDataCallback(builder, static_cast<int32_t>(block_size));
    AAudioStreamBuilder_setDataCallback(builder, onAudioReady, this);
    
    result = AAudioStreamBuilder_openStream(builder, &stream_);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK || !stream_) return false;
    
    actual_sample_rate_ = static_cast<uint32_t>(AAudioStream_getSampleRate(stream_));
    actual_block_size_  = static_cast<size_t>(AAudioStream_getFramesPerDataCallback(stream_));
    return true;
}

inline bool AudioEngine::play() {
    if (!stream_) return false;
    aaudio_result_t r = AAudioStream_requestStart(stream_);
    if (r != AAUDIO_OK) return false;
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
    return running_.load(std::memory_order_acquire);
}

inline aaudio_data_callback_result_t AudioEngine::onAudioReady(
    AAudioStream*, void* userdata, void* audioData, int32_t numFrames)
{
    auto* e = static_cast<AudioEngine*>(userdata);
    auto* out = static_cast<float*>(audioData);
    
    if (e->cb_L_.size() < static_cast<size_t>(numFrames)) {
        e->cb_L_.resize(numFrames);
        e->cb_R_.resize(numFrames);
    }
    
    if (!e->player_) {
        for (int32_t i = 0; i < numFrames * e->channels_; ++i) out[i] = 0.0f;
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    
    e->player_->process_block(e->cb_L_.data(), e->cb_R_.data());
    
    if (e->channels_ == 2) {
        for (int32_t i = 0; i < numFrames; ++i) {
            out[i*2+0] = e->cb_L_[i];
            out[i*2+1] = e->cb_R_[i];
        }
    } else {
        for (int32_t i = 0; i < numFrames; ++i) out[i] = e->cb_L_[i];
    }
    
    if (e->player_->finished()) {
        e->running_.store(false, std::memory_order_release);
        return AAUDIO_CALLBACK_RESULT_STOP;
    }
    
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// ─────────────────────────────────────────────────────────────
// WINDOWS (WASAPI)
// ──────────────────────────────────────────────────────────────
#elif defined(_WIN32)

inline bool AudioEngine::start(uint32_t /*sample_rate*/, int channels, size_t block_size, RealtimePlayer* player) {
    player_ = player; channels_ = channels; block_size_ = block_size;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator_));
    if (FAILED(hr) || !enumerator_) return false;

    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr) || !device_) return false;

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&audio_client_));
    if (FAILED(hr) || !audio_client_) return false;

    WAVEFORMATEX* mix_format = nullptr;
    hr = audio_client_->GetMixFormat(&mix_format);
    if (FAILED(hr) || !mix_format) return false;

    bool is_float = (mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                    (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE);
    if (!is_float) { CoTaskMemFree(mix_format); return false; }

    REFERENCE_TIME buffer_duration = 1000000; // 100ms
    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                   buffer_duration, 0, mix_format, nullptr);
    if (FAILED(hr)) { CoTaskMemFree(mix_format); return false; }

    hr = audio_client_->GetService(__uuidof(IAudioRenderClient),
                                   reinterpret_cast<void**>(&render_client_));
    if (FAILED(hr) || !render_client_) { CoTaskMemFree(mix_format); return false; }

    buffer_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!buffer_event_) { CoTaskMemFree(mix_format); return false; }

    hr = audio_client_->SetEventHandle(buffer_event_);
    if (FAILED(hr)) { CoTaskMemFree(mix_format); return false; }

    actual_sample_rate_ = mix_format->nSamplesPerSec;
    channels_ = mix_format->nChannels;
    CoTaskMemFree(mix_format);

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
    SetEvent(buffer_event_);
    HRESULT hr = audio_client_->Start();
    if (FAILED(hr)) return false;
    running_.store(true, std::memory_order_release);
    audio_thread_ = std::thread(&AudioEngine::audio_thread_func, this);
    return true;
}

inline void AudioEngine::stop() {
    stop_requested_.store(true, std::memory_order_release);
    if (audio_thread_.joinable()) audio_thread_.join();
    if (audio_client_) audio_client_->Stop();
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
    // ... (mantenha o loop do WASAPI que já estava funcionando) ...
    // Para economizar espaço na resposta, assuma que o loop do Windows continua aqui.
    // Se precisar, é só colar o loop do WASAPI da versão anterior.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool com_initialized = SUCCEEDED(hr);
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
                if (SUCCEEDED(render_client_->GetBuffer(frames_to_write, &data)) && data) {
                    auto* out = reinterpret_cast<float*>(data);
                    UINT32 frames_filled = 0;
                    while (frames_filled < frames_to_write) {
                        UINT32 frames_this_iter = std::min(static_cast<UINT32>(block_size_), frames_to_write - frames_filled);
                        if (cb_L_.size() < frames_this_iter) { cb_L_.resize(frames_this_iter); cb_R_.resize(frames_this_iter); }
                        if (player_) {
                            size_t processed = player_->process_block(cb_L_.data(), cb_R_.data());
                            if (channels_ == 2) {
                                for (UINT32 i = 0; i < frames_this_iter; ++i) {
                                    if (i < processed) { out[(frames_filled + i)*2+0] = cb_L_[i]; out[(frames_filled + i)*2+1] = cb_R_[i]; }
                                    else { out[(frames_filled + i)*2+0] = 0.0f; out[(frames_filled + i)*2+1] = 0.0f; }
                                }
                            } else {
                                for (UINT32 i = 0; i < frames_this_iter; ++i) out[frames_filled + i] = (i < processed) ? cb_L_[i] : 0.0f;
                            }
                            frames_filled += static_cast<UINT32>(frames_this_iter);
                            if (player_->finished() && processed == 0) {
                                for (UINT32 i = frames_filled; i < frames_to_write; ++i) {
                                    if (channels_ == 2) { out[i*2+0] = 0.0f; out[i*2+1] = 0.0f; } else { out[i] = 0.0f; }
                                }
                                break;
                            }
                        } else {
                            std::memset(data, 0, frames_to_write * channels_ * sizeof(float));
                            break;
                        }
                    }
                    render_client_->ReleaseBuffer(frames_to_write, 0);
                }
            }
        } else if (wait_result == WAIT_TIMEOUT) {
            if (player_ && player_->finished()) { running_.store(false, std::memory_order_release); break; }
        } else { running_.store(false, std::memory_order_release); break; }
    }
    if (com_initialized) CoUninitialize();
}

// ──────────────────────────────────────────────────────────────
// LINUX (ALSA) - NOVO!
// ──────────────────────────────────────────────────────────────
#elif defined(__linux__)

inline bool AudioEngine::start(uint32_t sample_rate, int channels, size_t block_size, RealtimePlayer* player) {
    player_ = player;
    channels_ = channels;
    block_size_ = block_size;
    actual_sample_rate_ = sample_rate;
    actual_block_size_ = block_size;

    // Usar "default" roteia para PulseAudio/PipeWire no Kubuntu, 
    // que faz o resampling automático e mixagem com outros apps.
    int err = snd_pcm_open(&pcm_handle_, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::fprintf(stderr, "ALSA: cannot open audio device: %s\n", snd_strerror(err));
        return false;
    }

    snd_pcm_hw_params_t* params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm_handle_, params);

    snd_pcm_hw_params_set_access(pcm_handle_, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle_, params, SND_PCM_FORMAT_FLOAT_LE);
    snd_pcm_hw_params_set_channels(pcm_handle_, params, channels);

    unsigned int val = sample_rate;
    int dir = 0;
    err = snd_pcm_hw_params_set_rate_near(pcm_handle_, params, &val, &dir);
    if (err < 0) {
        std::fprintf(stderr, "ALSA: cannot set sample rate: %s\n", snd_strerror(err));
        snd_pcm_close(pcm_handle_);
        return false;
    }
    actual_sample_rate_ = val;

    // Configurar tamanho do período e buffer para baixa latência
    snd_pcm_uframes_t frames = block_size;
    snd_pcm_hw_params_set_period_size_near(pcm_handle_, params, &frames, &dir);
    frames = block_size * 4; // Buffer de 4 períodos
    snd_pcm_hw_params_set_buffer_size_near(pcm_handle_, params, &frames);

    err = snd_pcm_hw_params(pcm_handle_, params);
    if (err < 0) {
        std::fprintf(stderr, "ALSA: cannot set parameters: %s\n", snd_strerror(err));
        snd_pcm_close(pcm_handle_);
        return false;
    }

    snd_pcm_prepare(pcm_handle_);
    return true;
}

inline bool AudioEngine::play() {
    if (!pcm_handle_) return false;
    stop_requested_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    playback_thread_ = std::thread(&AudioEngine::alsa_playback_loop, this);
    return true;
}

inline void AudioEngine::stop() {
    stop_requested_.store(true, std::memory_order_release);
    if (playback_thread_.joinable()) playback_thread_.join();
    if (pcm_handle_) {
        snd_pcm_drop(pcm_handle_);
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
    running_.store(false, std::memory_order_release);
}

inline bool AudioEngine::isRunning() const {
    return running_.load(std::memory_order_acquire);
}

inline void AudioEngine::alsa_playback_loop() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
        if (cb_L_.size() < block_size_) {
            cb_L_.resize(block_size_);
            cb_R_.resize(block_size_);
        }

        if (player_) {
            size_t processed = player_->process_block(cb_L_.data(), cb_R_.data());
            
            if (processed > 0) {
                // Interleaved buffer para o ALSA
                std::vector<float> interleaved(processed * channels_);
                if (channels_ == 2) {
                    for (size_t i = 0; i < processed; ++i) {
                        interleaved[i * 2 + 0] = cb_L_[i];
                        interleaved[i * 2 + 1] = cb_R_[i];
                    }
                } else {
                    for (size_t i = 0; i < processed; ++i) interleaved[i] = cb_L_[i];
                }

                snd_pcm_sframes_t frames_written = snd_pcm_writei(pcm_handle_, interleaved.data(), processed);
                
                // Lidar com underrun (-EPIPE)
                if (frames_written == -EPIPE) {
                    std::fprintf(stderr, "ALSA: buffer underrun occurred\n");
                    snd_pcm_prepare(pcm_handle_);
                } else if (frames_written < 0) {
                    std::fprintf(stderr, "ALSA: error from writei: %s\n", snd_strerror(frames_written));
                    break;
                }
            }

            if (player_->finished() && processed == 0) {
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
#endif

} // namespace slac::rt