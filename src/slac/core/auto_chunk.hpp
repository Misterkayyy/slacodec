#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
#include <cmath>

namespace slac {
namespace core {

// Parâmetros automatizáveis no chunk 'auto'.
enum class AutoParam : uint8_t {
    WidenessPermille = 0,
    ReverbWetPct     = 1,
    HrirMixPct       = 2,   // reservado por enquanto
};

struct AutoKeyframe {
    uint32_t sample_offset = 0; // offset absoluto em samples PCM
    uint8_t  param_id      = 0; // AutoParam
    float    value         = 0.0f;
};

// Cabeçalho fixo do payload do chunk 'auto'.
// u16 version (1)
// u16 reserved (0)
// u32 keyframe_count
struct AutoChunkHeader {
    uint16_t version        = 1;
    uint16_t reserved       = 0;
    uint32_t keyframe_count = 0;
};

constexpr size_t kAutoChunkHeaderSize = 8;
constexpr size_t kAutoKeyframeSize    = 9; // 4 + 1 + 4

namespace detail {

inline uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline void write_u16_le(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

inline void write_u32_le(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

} // namespace detail

// Checa se os keyframes estão estritamente ordenados por (sample_offset, param_id).
inline bool auto_chunk_is_sorted(const std::vector<AutoKeyframe>& keyframes) {
    for (size_t i = 1; i < keyframes.size(); ++i) {
        const auto& a = keyframes[i - 1];
        const auto& b = keyframes[i];
        if (a.sample_offset > b.sample_offset) return false;
        if (a.sample_offset == b.sample_offset && a.param_id >= b.param_id) return false;
    }
    return true;
}

// Serializa keyframes em um buffer de bytes (little-endian).
// Os keyframes DEVEM estar ordenados por (sample_offset, param_id) e sem duplicatas.
inline bool auto_chunk_serialize(const std::vector<AutoKeyframe>& keyframes, std::vector<uint8_t>& out) {
    if (!auto_chunk_is_sorted(keyframes)) {
        return false; // Encoder bug: keyframes fora de ordem
    }

    const size_t count = keyframes.size();
    if (count > 0xFFFFFFFFULL) return false; // Estouro

    const size_t payload_size = kAutoChunkHeaderSize + count * kAutoKeyframeSize;
    out.resize(payload_size);

    uint8_t* p = out.data();
    detail::write_u16_le(p, 1); // version
    detail::write_u16_le(p + 2, 0); // reserved
    detail::write_u32_le(p + 4, static_cast<uint32_t>(count));

    p += kAutoChunkHeaderSize;
    for (const auto& kf : keyframes) {
        if (kf.param_id > 2) return false;
        if (!std::isfinite(kf.value)) return false;

        detail::write_u32_le(p, kf.sample_offset);
        p[4] = kf.param_id;
        std::memcpy(p + 5, &kf.value, 4);
        p += kAutoKeyframeSize;
    }

    return true;
}

// Parseia o payload do chunk 'auto'.
// Valida: tamanho, version, reserved, IDs conhecidos, finitude (NaN/Inf) e ordenação estrita.
inline bool auto_chunk_parse(const uint8_t* data, size_t size, std::vector<AutoKeyframe>& out) {
    out.clear();

    if (size < kAutoChunkHeaderSize) return false;

    const uint16_t version = detail::read_u16_le(data);
    const uint16_t reserved = detail::read_u16_le(data + 2);
    const uint32_t count = detail::read_u32_le(data + 4);

    if (version != 1) return false;
    if (reserved != 0) return false;

    const size_t expected_size = kAutoChunkHeaderSize + static_cast<size_t>(count) * kAutoKeyframeSize;
    if (size != expected_size) return false;

    out.reserve(count);
    const uint8_t* p = data + kAutoChunkHeaderSize;

    for (uint32_t i = 0; i < count; ++i) {
        AutoKeyframe kf;
        kf.sample_offset = detail::read_u32_le(p);
        kf.param_id = p[4];
        std::memcpy(&kf.value, p + 5, 4);
        p += kAutoKeyframeSize;

        if (kf.param_id > 2) return false;
        if (!std::isfinite(kf.value)) return false;

        out.push_back(kf);
    }

    if (!auto_chunk_is_sorted(out)) {
        out.clear();
        return false;
    }

    return true;
}

} // namespace core
} // namespace slac
