#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "auto_chunk.hpp"
#include "block.hpp"
#include "ms.hpp"
#include "crc32.hpp"
#include "crc16.hpp"
#include "frame.hpp"
#include "sha256.hpp"

namespace slac {

enum class DecorrelationMode : uint8_t {
    None = 0,
    MidSideFixed = 1,
    Adaptive = 2,
};

struct SlacFormat {
    uint32_t sampleRate    = 44100;
    uint8_t  bitsPerSample = 16;
    uint8_t  channelCount  = 1;
    uint64_t totalSamples  = 0;
    uint32_t frameSize     = DEFAULT_FRAME_SIZE;
    uint8_t  preset        = 0;
    DecorrelationMode mode = DecorrelationMode::None;
    uint8_t  encoderMajor  = 0;
    uint8_t  encoderMinor  = 1;
};

struct SpatMetadata {
    uint16_t wideness_permille = 1000;
    uint8_t  reverb_wet_pct    = 0;
    uint8_t  preset_id         = 0;
    uint8_t  flags             = 0;
    uint8_t  chain_order       = 0;
    uint8_t  fallback_category = 0;
    uint8_t  fallback_size     = 0;
    uint8_t  fallback_decay    = 0;
    uint8_t  reserved          = 0;
};

struct SeekTable {
    uint32_t frame_count = 0;
    uint32_t frame_size  = 0;
    std::vector<uint32_t> byte_offsets;
};

struct HashInfo {
    bool present = false;
    bool match   = false;
    std::vector<uint8_t> digest;
};

namespace detail {

inline constexpr uint32_t fourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
}

inline constexpr uint32_t kChunkSLAC = fourCC('S', 'L', 'A', 'C');
inline constexpr uint32_t kChunkFmt  = fourCC('f', 'm', 't', ' ');
inline constexpr uint32_t kChunkData = fourCC('d', 'a', 't', 'a');
inline constexpr uint32_t kChunkMeta = fourCC('m', 'e', 't', 'a');
inline constexpr uint32_t kChunkCovr = fourCC('c', 'o', 'v', 'r');
inline constexpr uint32_t kChunkSpat = fourCC('s', 'p', 'a', 't');
inline constexpr uint32_t kChunkAuto = fourCC('a', 'u', 't', 'o');
inline constexpr uint32_t kChunkSeek = fourCC('s', 'e', 'e', 'k');
inline constexpr uint32_t kChunkHash = fourCC('h', 'a', 's', 'h');

inline void appendU32LE(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
}

inline void appendU64LE(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
}

inline void appendChunk(std::vector<uint8_t>& out, uint32_t id,
                        const std::vector<uint8_t>& payload) {
    uint32_t payload_size = static_cast<uint32_t>(payload.size());

    uint32_t crc = crc32_calculate(reinterpret_cast<const uint8_t*>(&id), 4);
    crc = crc32_calculate(reinterpret_cast<const uint8_t*>(&payload_size), 4, crc);
    if (!payload.empty())
        crc = crc32_calculate(payload.data(), payload.size(), crc);

    appendU32LE(out, id);
    appendU32LE(out, payload_size);
    appendU32LE(out, crc);
    out.insert(out.end(), payload.begin(), payload.end());
}

inline uint8_t readU8(const uint8_t* d, size_t sz, size_t& p) {
    if (p + 1 > sz) throw std::out_of_range("readU8");
    return d[p++];
}

inline uint32_t readU32LE(const uint8_t* d, size_t sz, size_t& p) {
    if (p + 4 > sz) throw std::out_of_range("readU32LE");
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<uint32_t>(d[p + i]) << (8 * i);
    p += 4;
    return v;
}

inline uint64_t readU64LE(const uint8_t* d, size_t sz, size_t& p) {
    if (p + 8 > sz) throw std::out_of_range("readU64LE");
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(d[p + i]) << (8 * i);
    p += 8;
    return v;
}

inline void appendSpat(std::vector<uint8_t>& out, const SpatMetadata& s) {
    out.push_back(static_cast<uint8_t>(s.wideness_permille & 0xFF));
    out.push_back(static_cast<uint8_t>((s.wideness_permille >> 8) & 0xFF));
    out.push_back(s.reverb_wet_pct);
    out.push_back(s.preset_id);
    out.push_back(s.flags);
    out.push_back(s.chain_order);
    out.push_back(s.fallback_category);
    out.push_back(s.fallback_size);
    out.push_back(s.fallback_decay);
    out.push_back(s.reserved);
}

inline SpatMetadata readSpat(const uint8_t* d, size_t sz, size_t& p) {
    SpatMetadata s;
    s.wideness_permille = static_cast<uint16_t>(
        readU8(d, sz, p) | (static_cast<uint16_t>(readU8(d, sz, p)) << 8));
    s.reverb_wet_pct    = readU8(d, sz, p);
    s.preset_id         = readU8(d, sz, p);
    s.flags             = readU8(d, sz, p);
    s.chain_order       = readU8(d, sz, p);
    s.fallback_category = readU8(d, sz, p);
    s.fallback_size     = readU8(d, sz, p);
    s.fallback_decay    = readU8(d, sz, p);
    s.reserved          = readU8(d, sz, p);
    return s;
}

} // namespace detail

// ──────────────────────────────────────────────────────────────
// ENCODE
// ──────────────────────────────────────────────────────────────

inline std::vector<uint8_t> encodeSlacFile(
    const std::vector<std::vector<int32_t>>& channels,
    const SlacFormat& fmt_in,
    const SpatMetadata* spat = nullptr,
    int lpc_order = 8,
    int lpc_shift = 15,
    bool use_range_coding = false,
    const std::vector<core::AutoKeyframe>* auto_kfs = nullptr)
{
    if (channels.empty())
        throw std::invalid_argument("encodeSlacFile: no channels");
    if (channels.size() > 2)
        throw std::invalid_argument("encodeSlacFile: max 2 channels");

    size_t total_samples = channels[0].size();
    for (const auto& ch : channels) {
        if (ch.size() != total_samples)
            throw std::invalid_argument("encodeSlacFile: channel size mismatch");
    }

    SlacFormat f = fmt_in;
    f.channelCount = static_cast<uint8_t>(channels.size());
    f.totalSamples = static_cast<uint64_t>(total_samples);
    if (f.channelCount == 1) f.mode = DecorrelationMode::None;
    if (f.frameSize == 0) f.frameSize = DEFAULT_FRAME_SIZE;

    uint32_t frame_size = f.frameSize;
    uint32_t num_frames = static_cast<uint32_t>(
        (total_samples + frame_size - 1) / frame_size);
    bool adaptive = (f.channelCount == 2 && f.mode == DecorrelationMode::Adaptive);

    // Hash chunk: SHA-256 do PCM canonico (planar int32 LE)
    std::vector<uint8_t> canon = pcm_canonical_bytes(channels);
    std::vector<uint8_t> digest = sha256(canon.data(), canon.size());
    std::vector<uint8_t> hash_payload;
    hash_payload.push_back(1); // version
    hash_payload.push_back(0); // layout: planar int32 LE
    hash_payload.insert(hash_payload.end(), digest.begin(), digest.end());

    // ── Frames (paralelizado em múltiplos threads) ─────────────
    // Determinar número de threads (2-4 em Android quad-core)
    const unsigned int num_threads = std::min(
        4u,
        std::max(1u, std::thread::hardware_concurrency()));

    // Estrutura para armazenar resultado de cada thread
    struct ThreadResult {
        std::vector<uint8_t> bytes;
        std::vector<uint32_t> offsets;
    };

    std::vector<ThreadResult> results(num_threads);
    std::vector<std::thread> threads;

    // Frames por thread (divisão equilibrada)
    const uint32_t frames_per_thread = (num_frames + num_threads - 1) / num_threads;

    for (unsigned int t = 0; t < num_threads; ++t) {
        uint32_t start_frame = t * frames_per_thread;
        uint32_t end_frame = std::min(start_frame + frames_per_thread, num_frames);

        if (start_frame >= num_frames) break;

        threads.emplace_back([&, t, start_frame, end_frame]() {
            ThreadResult& result = results[t];
            result.bytes.reserve(4096 * (end_frame - start_frame) * f.channelCount / 2);
            result.offsets.reserve(end_frame - start_frame);

            for (uint32_t fi = start_frame; fi < end_frame; ++fi) {
                size_t start = static_cast<size_t>(fi) * frame_size;
                size_t count = std::min(static_cast<size_t>(frame_size),
                                        total_samples - start);

                result.offsets.push_back(static_cast<uint32_t>(result.bytes.size()));

                std::vector<std::vector<int32_t>> frame_streams;
                StereoMode frame_mode = StereoMode::MidSide;

                if (f.channelCount == 2) {
                    std::vector<int32_t> frame_l(channels[0].begin() + start,
                                                 channels[0].begin() + start + count);
                    std::vector<int32_t> frame_r(channels[1].begin() + start,
                                                 channels[1].begin() + start + count);

                    if (adaptive)
                        frame_mode = selectBestStereoMode(frame_l, frame_r);

                    std::vector<int32_t> s0, s1;
                    stereoEncode(frame_l, frame_r, frame_mode, s0, s1);

                    frame_streams.push_back(std::move(s0));
                    frame_streams.push_back(std::move(s1));
                } else {
                    frame_mode = StereoMode::LeftRight;
                    frame_streams.push_back(std::vector<int32_t>(
                        channels[0].begin() + start,
                        channels[0].begin() + start + count));
                }

                int bps = f.bitsPerSample;
                int s1bits = bps;
                if (f.channelCount == 2 && frame_mode != StereoMode::LeftRight)
                    s1bits = bps + 1;

                auto frame_bytes = encode_frame(frame_streams, bps,
                                                lpc_order, lpc_shift, frame_mode,
                                                use_range_coding, s1bits);

                result.bytes.insert(result.bytes.end(),
                                    frame_bytes.begin(), frame_bytes.end());
            }
        });
    }

    // Aguardar todas as threads terminarem
    for (auto& thread : threads) {
        thread.join();
    }

    // Concatenar resultados e calcular offsets globais
    std::vector<uint8_t> data_payload;
    std::vector<uint32_t> frame_offsets;
    frame_offsets.reserve(num_frames);

    uint32_t cumulative_offset = 0;
    for (const auto& result : results) {
        for (uint32_t local_offset : result.offsets) {
            frame_offsets.push_back(cumulative_offset + local_offset);
        }
        data_payload.insert(data_payload.end(),
                            result.bytes.begin(), result.bytes.end());
        cumulative_offset += static_cast<uint32_t>(result.bytes.size());
    }

    // Invariante: nunca emitir arquivo com audio nao-vazio e data vazio.
    if (total_samples > 0 && data_payload.empty())
        throw std::runtime_error("encodeSlacFile: internal error, empty data chunk");

    // ── seek chunk ──────────────────────────────────────────
    std::vector<uint8_t> seek_payload;
    detail::appendU32LE(seek_payload, num_frames);
    detail::appendU32LE(seek_payload, frame_size);
    for (uint32_t off : frame_offsets)
        detail::appendU32LE(seek_payload, off);

    // ── fmt chunk ───────────────────────────────────────────
    std::vector<uint8_t> fmt_payload;
    detail::appendU32LE(fmt_payload, f.sampleRate);
    fmt_payload.push_back(f.bitsPerSample);
    fmt_payload.push_back(f.channelCount);
    fmt_payload.push_back(static_cast<uint8_t>(f.mode));
    fmt_payload.push_back(0);
    detail::appendU64LE(fmt_payload, f.totalSamples);
    detail::appendU32LE(fmt_payload, f.frameSize);
    fmt_payload.push_back(f.preset);
    fmt_payload.push_back(f.encoderMajor);
    fmt_payload.push_back(f.encoderMinor);
    fmt_payload.push_back(0);

    // ── spat chunk ──────────────────────────────────────────
    std::vector<uint8_t> spat_payload;
    if (spat)
        detail::appendSpat(spat_payload, *spat);

    // ── auto chunk ──────────────────────────────────────────
    std::vector<uint8_t> auto_payload;
    if (auto_kfs && !auto_kfs->empty()) {
        if (!core::auto_chunk_serialize(*auto_kfs, auto_payload)) {
            throw std::runtime_error("encodeSlacFile: failed to serialize auto chunk");
        }
    }

    // ── SLAC chunk ──────────────────────────────────────────
    std::vector<uint8_t> slac_payload = {0, 1, 0, 0};

    // ── montagem ────────────────────────────────────────────
    std::vector<uint8_t> file;
    detail::appendChunk(file, detail::kChunkSLAC, slac_payload);
    detail::appendChunk(file, detail::kChunkFmt,  fmt_payload);
    if (spat)
        detail::appendChunk(file, detail::kChunkSpat, spat_payload);
    if (auto_kfs && !auto_kfs->empty())
        detail::appendChunk(file, detail::kChunkAuto, auto_payload);
    detail::appendChunk(file, detail::kChunkSeek, seek_payload);
    detail::appendChunk(file, detail::kChunkHash, hash_payload);
    detail::appendChunk(file, detail::kChunkData, data_payload);

    return file;
}

// ──────────────────────────────────────────────────────────────
// DECODE
// ──────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────
// METADATA: chunk 'meta' estilo Vorbis Comments (CHAVE=valor)
// Forward-compatible: decodificadores antigos ignoram este chunk.
// ──────────────────────────────────────────────────────────────

struct SlacMetadata {
    std::vector<std::pair<std::string, std::string>> fields;
    void set(const std::string& k, const std::string& v) {
        for (auto& f : fields) if (f.first == k) { f.second = v; return; }
        fields.emplace_back(k, v);
    }
    const std::string* get(const std::string& k) const {
        for (auto& f : fields) if (f.first == k) return &f.second;
        return nullptr;
    }
};

namespace detail {
inline std::vector<uint8_t> serializeMeta(const SlacMetadata& m) {
    std::vector<uint8_t> out;
    for (auto& kv : m.fields) {
        appendU32LE(out, static_cast<uint32_t>(kv.first.size()));
        out.insert(out.end(), kv.first.begin(), kv.first.end());
        appendU32LE(out, static_cast<uint32_t>(kv.second.size()));
        out.insert(out.end(), kv.second.begin(), kv.second.end());
    }
    return out;
}
inline bool deserializeMeta(const uint8_t* p, size_t n, SlacMetadata& m) {
    size_t pos = 0;
    while (pos + 4 <= n) {
        uint32_t kl = readU32LE(p, n, pos);
        if (kl > n - pos) return false;
        std::string k(reinterpret_cast<const char*>(p + pos), kl); pos += kl;
        if (pos + 4 > n) return false;
        uint32_t vl = readU32LE(p, n, pos);
        if (vl > n - pos) return false;
        std::string v(reinterpret_cast<const char*>(p + pos), vl); pos += vl;
        m.fields.emplace_back(std::move(k), std::move(v));
    }
    return true;
}
} // namespace detail

inline bool readMetaFromFile(const std::string& path, SlacMetadata& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    while (pos + 12 <= buf.size()) {
        uint32_t id = detail::readU32LE(buf.data(), buf.size(), pos);
        uint32_t sz = detail::readU32LE(buf.data(), buf.size(), pos);
        pos += 4; // crc
        if (pos + sz > buf.size()) return false;
        if (id == detail::kChunkMeta)
            return detail::deserializeMeta(buf.data() + pos, sz, out);
        pos += sz;
    }
    return true; // sem chunk meta = metadata vazia
}

// Remux: reescreve o container injetando/substituindo o chunk meta
// SEM re-encodar o audio (copia os payloads intactos).
inline bool addMetaToFile(const std::string& path, const SlacMetadata& meta) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::vector<uint8_t> out;
    std::vector<uint8_t> metaPayload = detail::serializeMeta(meta);
    bool metaWritten = false;
    size_t pos = 0;
    while (pos + 12 <= buf.size()) {
        uint32_t id = detail::readU32LE(buf.data(), buf.size(), pos);
        uint32_t sz = detail::readU32LE(buf.data(), buf.size(), pos);
        pos += 4; // crc
        if (pos + sz > buf.size()) return false;
        std::vector<uint8_t> payload(buf.begin() + pos, buf.begin() + pos + sz);
        pos += sz;
        if (id == detail::kChunkMeta) continue; // descarta meta antigo
        detail::appendChunk(out, id, payload);
        if (!metaWritten && id == detail::kChunkFmt) {
            detail::appendChunk(out, detail::kChunkMeta, metaPayload);
            metaWritten = true;
        }
    }
    if (!metaWritten)
        detail::appendChunk(out, detail::kChunkMeta, metaPayload);

    std::ofstream o(path + ".tmp", std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    o.close();
    std::remove(path.c_str());
    return std::rename((path + ".tmp").c_str(), path.c_str()) == 0;
}

// ──────────────────────────────────────────────────────────────
// COVER ART: chunk 'covr' (mime + bytes da imagem embutida)
// ──────────────────────────────────────────────────────────────
struct SlacCover {
    std::string mime;
    std::vector<uint8_t> data;
};

inline bool readCoverFromFile(const std::string& path, SlacCover& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    while (pos + 12 <= buf.size()) {
        uint32_t id = detail::readU32LE(buf.data(), buf.size(), pos);
        uint32_t sz = detail::readU32LE(buf.data(), buf.size(), pos);
        pos += 4; // crc
        if (pos + sz > buf.size()) return false;
        if (id == detail::kChunkCovr) {
            size_t q = pos;
            uint32_t ml = detail::readU32LE(buf.data(), buf.size(), q);
            if (q + ml > pos + sz) return false;
            out.mime.assign(reinterpret_cast<const char*>(buf.data() + q), ml);
            q += ml;
            out.data.assign(buf.begin() + q, buf.begin() + pos + sz);
            return true;
        }
        pos += sz;
    }
    return true; // sem capa = ok
}

inline bool addCoverToFile(const std::string& path, const std::string& mime,
                           const std::vector<uint8_t>& img) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::vector<uint8_t> payload;
    detail::appendU32LE(payload, static_cast<uint32_t>(mime.size()));
    payload.insert(payload.end(), mime.begin(), mime.end());
    payload.insert(payload.end(), img.begin(), img.end());

    std::vector<uint8_t> out;
    bool covrWritten = false;
    size_t pos = 0;
    while (pos + 12 <= buf.size()) {
        uint32_t id = detail::readU32LE(buf.data(), buf.size(), pos);
        uint32_t sz = detail::readU32LE(buf.data(), buf.size(), pos);
        pos += 4; // crc
        if (pos + sz > buf.size()) return false;
        std::vector<uint8_t> chunkPayload(buf.begin() + pos, buf.begin() + pos + sz);
        pos += sz;
        if (id == detail::kChunkCovr) continue; // descarta capa antiga
        detail::appendChunk(out, id, chunkPayload);
        if (!covrWritten && id == detail::kChunkFmt) {
            detail::appendChunk(out, detail::kChunkCovr, payload);
            covrWritten = true;
        }
    }
    if (!covrWritten)
        detail::appendChunk(out, detail::kChunkCovr, payload);

    std::ofstream o(path + ".tmp", std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    o.close();
    std::remove(path.c_str());
    return std::rename((path + ".tmp").c_str(), path.c_str()) == 0;
}

inline std::vector<std::vector<int32_t>> decodeSlacFile(
    const std::vector<uint8_t>& file,
    SlacFormat* out_fmt = nullptr,
    SpatMetadata* out_spat = nullptr,
    SeekTable* out_seek = nullptr,
    HashInfo* out_hash = nullptr,
    bool strict_crc = true,
    std::vector<core::AutoKeyframe>* out_auto = nullptr)
{
    if (file.size() < 12)
        throw std::invalid_argument("decodeSlacFile: too small");

    bool saw_slac = false, saw_fmt = false, saw_data = false;
    SlacFormat fmt;
    SpatMetadata spat;
    SeekTable seek;

    const uint8_t* data_payload = nullptr;
    size_t data_size = 0;
    const uint8_t* hash_payload = nullptr;
    size_t hash_size = 0;

    size_t pos = 0;

    while (pos < file.size()) {
        if (pos + 12 > file.size())
            throw std::runtime_error("decodeSlacFile: truncated chunk header");

        size_t header_start = pos;
        uint32_t id         = detail::readU32LE(file.data(), file.size(), pos);
        uint32_t payload_sz = detail::readU32LE(file.data(), file.size(), pos);
        uint32_t stored_crc = detail::readU32LE(file.data(), file.size(), pos);

        if (pos == 12 && id != detail::kChunkSLAC)
            throw std::runtime_error("decodeSlacFile: first chunk must be SLAC");

        if (pos + static_cast<size_t>(payload_sz) > file.size())
            throw std::runtime_error("decodeSlacFile: chunk out of range");

        if (strict_crc) {
            uint32_t calc = crc32_calculate(file.data() + header_start, 8);
            if (payload_sz > 0)
                calc = crc32_calculate(file.data() + pos, payload_sz, calc);
            if (calc != stored_crc)
                throw std::runtime_error("decodeSlacFile: chunk CRC mismatch");
        }

        const uint8_t* payload = file.data() + pos;

        if (id == detail::kChunkSLAC) {
            if (payload_sz < 4)
                throw std::runtime_error("SLAC chunk too small");
            if (payload[0] != 0)
                throw std::runtime_error("unsupported major version");
            saw_slac = true;
        }
        else if (id == detail::kChunkFmt) {
            if (payload_sz < 24)
                throw std::runtime_error("fmt chunk too small");
            size_t p = 0;
            fmt.sampleRate    = detail::readU32LE(payload, payload_sz, p);
            fmt.bitsPerSample = detail::readU8(payload, payload_sz, p);
            fmt.channelCount  = detail::readU8(payload, payload_sz, p);
            fmt.mode          = static_cast<DecorrelationMode>(
                                    detail::readU8(payload, payload_sz, p));
            detail::readU8(payload, payload_sz, p);
            fmt.totalSamples  = detail::readU64LE(payload, payload_sz, p);
            fmt.frameSize     = detail::readU32LE(payload, payload_sz, p);
            fmt.preset        = detail::readU8(payload, payload_sz, p);
            fmt.encoderMajor  = detail::readU8(payload, payload_sz, p);
            fmt.encoderMinor  = detail::readU8(payload, payload_sz, p);
            saw_fmt = true;
        }
        else if (id == detail::kChunkSpat) {
            if (payload_sz >= 10) {
                size_t p = 0;
                spat = detail::readSpat(payload, payload_sz, p);
            }
        }
        else if (id == detail::kChunkAuto) {
            if (out_auto) {
                std::vector<core::AutoKeyframe> parsed;
                if (core::auto_chunk_parse(payload, payload_sz, parsed)) {
                    *out_auto = std::move(parsed);
                } else {
                    throw std::runtime_error("decodeSlacFile: invalid auto chunk payload");
                }
            }
        }
        else if (id == detail::kChunkSeek) {
            size_t p = 0;
            seek.frame_count = detail::readU32LE(payload, payload_sz, p);
            seek.frame_size  = detail::readU32LE(payload, payload_sz, p);
            seek.byte_offsets.resize(seek.frame_count);
            for (uint32_t i = 0; i < seek.frame_count; ++i)
                seek.byte_offsets[i] = detail::readU32LE(payload, payload_sz, p);
        }
        else if (id == detail::kChunkHash) {
            hash_payload = payload;
            hash_size    = payload_sz;
        }
        else if (id == detail::kChunkData) {
            data_payload = payload;
            data_size    = payload_sz;
            saw_data = true;
        }
        // chunks desconhecidos: pular (forward compatibility)

        pos += static_cast<size_t>(payload_sz);
    }

    if (!saw_slac || !saw_fmt || !saw_data)
        throw std::runtime_error("decodeSlacFile: missing required chunk");

    if (out_fmt)  *out_fmt  = fmt;
    if (out_spat) *out_spat = spat;
    if (out_seek) *out_seek = seek;

    // ── frames ──────────────────────────────────────────────
    std::vector<std::vector<int32_t>> out_channels(fmt.channelCount);

    if (data_size == 0) {
        // arquivo com data vazio: preenche hash info e retorna vazio
        if (out_hash && hash_payload && hash_size >= 34) {
            out_hash->present = true;
            out_hash->digest.assign(hash_payload + 2, hash_payload + 34);
            out_hash->match = false;
        }
        return out_channels;
    }

    size_t offset = 0;
    uint32_t frames_decoded = 0;

    while (offset < data_size) {
        auto result = decode_frame(data_payload + offset,
                                   data_size - offset,
                                   fmt.bitsPerSample);

        if (!result.valid) {
            throw std::runtime_error(
                "decodeSlacFile: frame " + std::to_string(frames_decoded) +
                " decode error (code " + std::to_string(result.error) + ")");
        }

        if (fmt.channelCount == 2 && result.num_streams == 2) {
            std::vector<int32_t> frame_l, frame_r;
            stereoDecode(result.streams[0], result.streams[1],
                         result.stereo_mode, frame_l, frame_r);
            out_channels[0].insert(out_channels[0].end(),
                                   frame_l.begin(), frame_l.end());
            out_channels[1].insert(out_channels[1].end(),
                                   frame_r.begin(), frame_r.end());
        } else if (fmt.channelCount == 1 && result.num_streams == 1) {
            out_channels[0].insert(out_channels[0].end(),
                                   result.streams[0].begin(),
                                   result.streams[0].end());
        }

        offset += result.bytes_consumed;
        ++frames_decoded;
    }

    if (out_hash && hash_payload && hash_size >= 34) {
        out_hash->present = true;
        out_hash->digest.assign(hash_payload + 2, hash_payload + 34);
        std::vector<uint8_t> canon = pcm_canonical_bytes(out_channels);
        std::vector<uint8_t> calc = sha256(canon.data(), canon.size());
        out_hash->match = (calc == out_hash->digest);
    }

    return out_channels;
}

} // namespace slac
