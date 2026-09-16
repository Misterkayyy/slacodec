#pragma once

#include <cstdint>
#include <cstddef>

namespace slac {

inline uint32_t crc32_calculate(const uint8_t* data, size_t length, uint32_t crc = 0xFFFFFFFF) {
    static uint32_t table[256];
    static bool init = false;
    
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        init = true;
    }

    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

} // namespace slac
