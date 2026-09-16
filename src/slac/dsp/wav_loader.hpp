#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace slac::dsp {

struct WavData {
    uint32_t sample_rate = 0;
    uint16_t num_channels = 0;
    uint32_t num_frames = 0;

    // Interleaved float32 samples, normalized to [-1.0, +1.0].
    // Total size = num_frames * num_channels.
    std::vector<float> samples;

    // De-interleaved access: channel_data[ch] is a vector of num_frames.
    std::vector<std::vector<float>> channels() const {
        std::vector<std::vector<float>> ch(num_channels);
        for (int c = 0; c < num_channels; ++c) {
            ch[c].resize(num_frames);
            for (uint32_t i = 0; i < num_frames; ++i) {
                ch[c][i] = samples[i * num_channels + c];
            }
        }
        return ch;
    }
};

// Minimal WAV loader. Supports PCM 16/24/32-bit int and IEEE float 32.
inline WavData load_wav(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("load_wav: cannot open file: " + path);
    }

    // Read entire file into memory (IRs are small, this is fine).
    file.seekg(0, std::ios::end);
    size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> raw(file_size);
    file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(file_size));

    if (file_size < 44) {
        throw std::runtime_error("load_wav: file too small");
    }

    // ── Parse RIFF header ───────────────────────────────────
    auto read_fourcc = [&](size_t offset) -> uint32_t {
        uint32_t v;
        std::memcpy(&v, raw.data() + offset, 4);
        return v;
    };

    auto read_u32 = [&](size_t offset) -> uint32_t {
        uint32_t v;
        std::memcpy(&v, raw.data() + offset, 4);
        return v;
    };

    auto read_u16 = [&](size_t offset) -> uint16_t {
        uint16_t v;
        std::memcpy(&v, raw.data() + offset, 2);
        return v;
    };

    uint32_t riff_id = read_fourcc(0);
    uint32_t wave_id = read_fourcc(8);

    // "RIFF" = 0x46464952 in little-endian memory, "WAVE" = 0x45564157
    if (riff_id != 0x46464952 || wave_id != 0x45564157) {
        throw std::runtime_error("load_wav: not a valid RIFF/WAVE file");
    }

    // ── Walk chunks ─────────────────────────────────────────
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;

    const uint8_t* data_ptr = nullptr;
    uint32_t data_size = 0;

    size_t pos = 12; // after "RIFF" + size + "WAVE"

    while (pos + 8 <= file_size) {
        uint32_t chunk_id = read_fourcc(pos);
        uint32_t chunk_size = read_u32(pos + 4);

        if (pos + 8 + chunk_size > file_size) {
            throw std::runtime_error("load_wav: chunk exceeds file size");
        }

        if (chunk_id == 0x20746D66) { // "fmt "
            audio_format = read_u16(pos + 8);
            num_channels = read_u16(pos + 8 + 2);
            sample_rate = read_u32(pos + 8 + 4);
            bits_per_sample = read_u16(pos + 8 + 14);
        }
        else if (chunk_id == 0x61746164) { // "data"
            data_ptr = raw.data() + pos + 8;
            data_size = chunk_size;
        }

        // Chunks are word-aligned.
        pos += 8 + chunk_size + (chunk_size & 1);
    }

    if (data_ptr == nullptr || data_size == 0) {
        throw std::runtime_error("load_wav: no data chunk found");
    }

    if (audio_format != 1 && audio_format != 3) {
        throw std::runtime_error("load_wav: unsupported audio format (only PCM and IEEE float)");
    }

    if (num_channels == 0 || num_channels > 8) {
        throw std::runtime_error("load_wav: invalid channel count");
    }

    // ── Convert to float32 ──────────────────────────────────
    uint32_t bytes_per_sample = bits_per_sample / 8;
    uint32_t num_frames = data_size / (bytes_per_sample * num_channels);

    WavData wav;
    wav.sample_rate = sample_rate;
    wav.num_channels = num_channels;
    wav.num_frames = num_frames;
    wav.samples.resize(num_frames * num_channels);

    for (uint32_t i = 0; i < num_frames * num_channels; ++i) {
        size_t byte_offset = static_cast<size_t>(i) * bytes_per_sample;
        float value = 0.0f;

        if (audio_format == 3 && bits_per_sample == 32) {
            // IEEE float 32
            float f;
            std::memcpy(&f, data_ptr + byte_offset, 4);
            value = f;
        }
        else if (bits_per_sample == 16) {
            int16_t s;
            std::memcpy(&s, data_ptr + byte_offset, 2);
            value = static_cast<float>(s) / 32768.0f;
        }
        else if (bits_per_sample == 24) {
            int32_t s = (static_cast<int32_t>(data_ptr[byte_offset + 0]) << 0) |
                        (static_cast<int32_t>(data_ptr[byte_offset + 1]) << 8) |
                        (static_cast<int32_t>(data_ptr[byte_offset + 2]) << 16);
            // Sign-extend from 24-bit.
            if (s & 0x800000) s |= ~0xFFFFFF;
            value = static_cast<float>(s) / 8388608.0f;
        }
        else if (bits_per_sample == 32) {
            int32_t s;
            std::memcpy(&s, data_ptr + byte_offset, 4);
            value = static_cast<float>(s) / 2147483648.0f;
        }
        else {
            throw std::runtime_error("load_wav: unsupported bit depth");
        }

        wav.samples[i] = value;
    }

    return wav;
}

} // namespace slac::dsp
