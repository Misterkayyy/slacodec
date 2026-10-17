#pragma once

#include "slac/core/crc32.hpp"
#include "slac/dsp/wav_loader.hpp"
#include "slac/dsp/analyzer.hpp"
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>

namespace slac::core {

constexpr uint32_t kChunkHIRR = 0x52524948; // "HIRR"

struct EmbeddedHRIR {
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    slac::dsp::HRIRType type = slac::dsp::HRIRType::None;
    uint32_t num_samples = 0;
    
    // Dados em float32 (planar)
    std::vector<float> ll, lr, rl, rr;
    
    bool present() const { return type != slac::dsp::HRIRType::None && num_samples > 0; }
};

// Injeta o chunk HIRR no vetor slac_data existente, logo antes do chunk "data"
inline void embed_hrir_into_slac(std::vector<uint8_t>& slac_data, const std::string& wav_path, slac::dsp::HRIRType ir_type) {
    slac::dsp::WavData wav = slac::dsp::load_wav(wav_path);
    if (wav.num_channels < 2) return;

    EmbeddedHRIR hrir;
    hrir.sample_rate = wav.sample_rate;
    hrir.channels = std::min(static_cast<uint8_t>(wav.num_channels), static_cast<uint8_t>(4));
    hrir.type = ir_type;
    hrir.num_samples = wav.num_frames;

    auto chs = wav.channels();
    hrir.ll.resize(hrir.num_samples);
    hrir.rr.resize(hrir.num_samples);
    if (hrir.channels >= 4) {
        hrir.lr.resize(hrir.num_samples);
        hrir.rl.resize(hrir.num_samples);
    }

    for (uint32_t i = 0; i < hrir.num_samples; ++i) {
        hrir.ll[i] = chs[0][i];
        hrir.rr[i] = chs[1][i];
        if (hrir.channels >= 4) {
            hrir.lr[i] = chs[2][i];
            hrir.rl[i] = chs[3][i];
        }
    }

    // 1. Montar o payload
    std::vector<uint8_t> payload;
    auto push_u32_to = [&](std::vector<uint8_t>& vec, uint32_t v) {
        vec.push_back(v & 0xFF);
        vec.push_back((v >> 8) & 0xFF);
        vec.push_back((v >> 16) & 0xFF);
        vec.push_back((v >> 24) & 0xFF);
    };
    auto push_f32_to = [&](std::vector<uint8_t>& vec, float v) {
        uint32_t u; std::memcpy(&u, &v, 4);
        push_u32_to(vec, u);
    };

    push_u32_to(payload, hrir.sample_rate);
    payload.push_back(hrir.channels);
    payload.push_back(static_cast<uint8_t>(hrir.type));
    payload.push_back(0); // reserved
    payload.push_back(0); // reserved
    push_u32_to(payload, hrir.num_samples);

    for (uint32_t i = 0; i < hrir.num_samples; ++i) push_f32_to(payload, hrir.ll[i]);
    for (uint32_t i = 0; i < hrir.num_samples; ++i) push_f32_to(payload, hrir.rr[i]);
    if (hrir.channels >= 4) {
        for (uint32_t i = 0; i < hrir.num_samples; ++i) push_f32_to(payload, hrir.lr[i]);
        for (uint32_t i = 0; i < hrir.num_samples; ++i) push_f32_to(payload, hrir.rl[i]);
    }

    // 2. Montar o header (ID + size)
    std::vector<uint8_t> header;
    header.push_back('H'); header.push_back('I'); header.push_back('R'); header.push_back('R');
    push_u32_to(header, static_cast<uint32_t>(payload.size()));

    // 3. Calcular CRC sobre header (8 bytes) + payload
    uint32_t crc = crc32_calculate(header.data(), 8);
    crc = crc32_calculate(payload.data(), payload.size(), crc);

    // 4. Montar o chunk final: header + CRC + payload
    std::vector<uint8_t> chunk;
    chunk.insert(chunk.end(), header.begin(), header.end());
    push_u32_to(chunk, crc);
    chunk.insert(chunk.end(), payload.begin(), payload.end());

    // 5. Inserir antes do chunk "data"
    const uint8_t data_magic[] = {'d', 'a', 't', 'a'};
    auto it = std::search(slac_data.begin(), slac_data.end(), 
                          std::begin(data_magic), std::end(data_magic));
    
    if (it != slac_data.end()) {
        slac_data.insert(it, chunk.begin(), chunk.end());
    } else {
        slac_data.insert(slac_data.end(), chunk.begin(), chunk.end());
    }
}

} // namespace slac::core