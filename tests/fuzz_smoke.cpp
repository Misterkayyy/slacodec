#include <slac/core/container.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

static void try_decode(const uint8_t* d, size_t n) {
    try {
        slac::HashInfo hash;
        auto pcm = slac::decodeSlacFile(
            std::vector<uint8_t>(d, d + n), nullptr, nullptr, nullptr, &hash);
        (void)pcm;
    } catch (...) {
        // esperado para entrada invalida: o decoder deve lancar, nunca crashar
    }
}

int main() {
    size_t n = 4096;
    std::vector<int32_t> mono(n);
    for (size_t i = 0; i < n; ++i)
        mono[i] = static_cast<int32_t>(5000.0 * std::sin(2.0 * 3.14159 * 440.0 * i / 48000.0));

    slac::SlacFormat fmt;
    fmt.sampleRate = 48000;
    fmt.bitsPerSample = 16;
    fmt.channelCount = 1;
    fmt.frameSize = 4096;

    auto file = slac::encodeSlacFile({mono}, fmt);

    std::cout << "[..] fuzz smoke: truncations\n";
    for (size_t cut = 0; cut < file.size(); cut += 97)
        try_decode(file.data(), cut);

    std::cout << "[..] fuzz smoke: bit flips\n";
    for (size_t pos = 0; pos < file.size(); pos += 13)
        for (int b = 0; b < 8; ++b) {
            std::vector<uint8_t> mut = file;
            mut[pos] ^= static_cast<uint8_t>(1 << b);
            try_decode(mut.data(), mut.size());
        }

    std::cout << "[..] fuzz smoke: garbage\n";
    uint32_t seed = 999;
    for (int iter = 0; iter < 200; ++iter) {
        seed = seed * 1103515245u + 12345u;
        size_t len = 1 + (seed % 512);
        std::vector<uint8_t> junk(len);
        for (size_t i = 0; i < len; ++i) {
            seed = seed * 1103515245u + 12345u;
            junk[i] = static_cast<uint8_t>(seed >> 16);
        }
        try_decode(junk.data(), junk.size());
    }

    std::cout << "[OK] fuzz smoke: no crash under mutations\n";
    return 0;
}
