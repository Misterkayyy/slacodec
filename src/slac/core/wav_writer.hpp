#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace slac {

// Write PCM data as a 16-bit PCM WAV file.
// channels[ch] is a vector of int32 samples (will be clamped to 16-bit).
inline void write_wav(
    const std::string& path,
    const std::vector<std::vector<int32_t>>& channels,
    uint32_t sample_rate
) {
    if (channels.empty()) {
        throw std::invalid_argument("write_wav: no channels");
    }

    uint16_t num_channels = static_cast<uint16_t>(channels.size());
    uint32_t num_frames = static_cast<uint32_t>(channels[0].size());

    for (const auto& ch : channels) {
        if (ch.size() != num_frames) {
            throw std::invalid_argument("write_wav: channel size mismatch");
        }
    }

    uint16_t bits_per_sample = 16;
    uint16_t bytes_per_sample = bits_per_sample / 8;
    uint32_t data_size = num_frames * num_channels * bytes_per_sample;

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("write_wav: cannot open file: " + path);
    }

    auto write_u32 = [&](uint32_t v) {
        file.write(reinterpret_cast<const char*>(&v), 4);
    };

    auto write_u16 = [&](uint16_t v) {
        file.write(reinterpret_cast<const char*>(&v), 2);
    };

    // ── RIFF header ─────────────────────────────────────────
    file.write("RIFF", 4);
    write_u32(36 + data_size);
    file.write("WAVE", 4);

    // ── fmt chunk ───────────────────────────────────────────
    file.write("fmt ", 4);
    write_u32(16);              // chunk size
    write_u16(1);               // PCM
    write_u16(num_channels);
    write_u32(sample_rate);
    write_u32(sample_rate * num_channels * bytes_per_sample); // byte rate
    write_u16(num_channels * bytes_per_sample);               // block align
    write_u16(bits_per_sample);

    // ── data chunk ──────────────────────────────────────────
    file.write("data", 4);
    write_u32(data_size);

    for (uint32_t i = 0; i < num_frames; ++i) {
        for (uint16_t c = 0; c < num_channels; ++c) {
            // Clamp to 16-bit range.
            int32_t s = channels[c][i];
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            int16_t sample = static_cast<int16_t>(s);
            file.write(reinterpret_cast<const char*>(&sample), 2);
        }
    }

    file.close();
}

} // namespace slac
