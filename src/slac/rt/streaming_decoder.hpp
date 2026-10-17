#pragma once

#include "slac/core/container.hpp"
#include "slac/core/crc32.hpp"
#include "slac/core/frame.hpp"
#include "slac/core/ms.hpp"
#include "slac/core/embedded_hrir.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace slac::rt {

namespace {
inline uint32_t readU32LE(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

inline uint64_t readU64LE(const uint8_t* data) {
    return static_cast<uint64_t>(readU32LE(data)) |
           (static_cast<uint64_t>(readU32LE(data + 4)) << 32);
}
}

class StreamingDecoder {
public:
    StreamingDecoder() = default;
    ~StreamingDecoder() { close(); }

    StreamingDecoder(const StreamingDecoder&) = delete;
    StreamingDecoder& operator=(const StreamingDecoder&) = delete;

    StreamingDecoder(StreamingDecoder&& other) noexcept { *this = std::move(other); }
    StreamingDecoder& operator=(StreamingDecoder&& other) noexcept {
        if (this != &other) {
            close();
            file_ = other.file_; other.file_ = nullptr;
            data_offset_ = other.data_offset_;
            current_frame_index_ = other.current_frame_index_;
            sample_pos_in_frame_ = other.sample_pos_in_frame_;
            global_sample_pos_ = other.global_sample_pos_;
            fmt_ = other.fmt_; spat_ = other.spat_; seek_ = other.seek_;
            auto_keyframes_ = std::move(other.auto_keyframes_);
            current_frame_ = std::move(other.current_frame_);
            current_frame_size_ = other.current_frame_size_;
        }
        return *this;
    }

    static bool open(const std::string& path, StreamingDecoder& out) {
        out.close();
        out.file_ = std::fopen(path.c_str(), "rb");
        if (!out.file_) return false;
        try {
            out.read_metadata();
            return true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[DEBUG] read_metadata exception: %s\n", e.what());
            out.close();
            return false;
        } catch (...) {
            std::fprintf(stderr, "[DEBUG] read_metadata unknown exception\n");
            out.close();
            return false;
        }
    }

    size_t read(std::vector<std::vector<int32_t>>& pcm, size_t count) {
        if (!file_ || count == 0) return 0;
        size_t channels = fmt_.channelCount;
        pcm.resize(channels);
        for (auto& ch : pcm) ch.clear();

        size_t total_read = 0;
        while (total_read < count) {
            if (current_frame_.empty() || sample_pos_in_frame_ >= current_frame_size_) {
                if (!decode_next_frame()) break;
            }
            size_t avail = current_frame_size_ - sample_pos_in_frame_;
            size_t to_copy = std::min(count - total_read, avail);
            for (size_t c = 0; c < channels; ++c) {
                const auto& src = current_frame_[c];
                auto it = src.begin() + sample_pos_in_frame_;
                pcm[c].insert(pcm[c].end(), it, it + to_copy);
            }
            sample_pos_in_frame_ += to_copy;
            global_sample_pos_ += to_copy;
            total_read += to_copy;
        }
        return total_read;
    }

    bool seek_to_sample(uint64_t sample) {
        if (!file_ || seek_.frame_count == 0) return false;
        uint32_t frame_size = seek_.frame_size;
        if (frame_size == 0) return false;
        
        uint64_t frame_index = sample / frame_size;
        if (frame_index >= seek_.frame_count) frame_index = seek_.frame_count - 1;
        
        uint64_t byte_offset = data_offset_ + seek_.byte_offsets[frame_index];
        if (std::fseek(file_, static_cast<long>(byte_offset), SEEK_SET) != 0) return false;
        
        current_frame_index_ = static_cast<uint32_t>(frame_index);
        sample_pos_in_frame_ = 0;
        global_sample_pos_ = frame_index * frame_size;
        current_frame_.clear();
        current_frame_size_ = 0;
        
        if (!decode_next_frame()) return false;
        
        uint64_t frame_start = frame_index * frame_size;
        uint64_t offset_in_frame = sample - frame_start;
        if (offset_in_frame > 0 && offset_in_frame < current_frame_size_) {
            sample_pos_in_frame_ = offset_in_frame;
            global_sample_pos_ = sample;
        }
        
        return true;
    }

    void close() {
        if (file_) { std::fclose(file_); file_ = nullptr; }
        data_offset_ = 0; current_frame_index_ = 0;
        sample_pos_in_frame_ = 0; global_sample_pos_ = 0;
        current_frame_.clear(); current_frame_size_ = 0;
    }

    uint32_t sample_rate() const { return fmt_.sampleRate; }
    uint8_t channels() const { return fmt_.channelCount; }
    uint8_t bits_per_sample() const { return fmt_.bitsPerSample; }
    uint64_t total_samples() const { return fmt_.totalSamples; }
    uint64_t position() const { return global_sample_pos_; }
    const slac::SpatMetadata& spat() const { return spat_; }
    const std::vector<slac::core::AutoKeyframe>& auto_keyframes() const { return auto_keyframes_; }
    const slac::core::EmbeddedHRIR& embedded_hrir() const { return embedded_hrir_; }

private:
    FILE* file_ = nullptr;
    uint64_t data_offset_ = 0;
    uint32_t current_frame_index_ = 0;
    size_t sample_pos_in_frame_ = 0;
    uint64_t global_sample_pos_ = 0;
    SlacFormat fmt_;
    SpatMetadata spat_;
    SeekTable seek_;
    std::vector<slac::core::AutoKeyframe> auto_keyframes_;
    std::vector<std::vector<int32_t>> current_frame_;
    size_t current_frame_size_ = 0;
    slac::core::EmbeddedHRIR embedded_hrir_;


    void read_metadata() {
        if (!file_) throw std::runtime_error("file not open");
        bool saw_slac = false, saw_fmt = false, saw_data = false;
        
        while (true) {
            uint8_t header[12];
            if (std::fread(header, 1, 12, file_) != 12) break;
            uint32_t id = readU32LE(header);
            uint32_t payload_sz = readU32LE(header + 4);
            uint32_t stored_crc = readU32LE(header + 8);
            
            if (id == detail::kChunkData) {
                data_offset_ = std::ftell(file_);
                saw_data = true;
                if (std::fseek(file_, data_offset_, SEEK_SET) != 0) {
                    throw std::runtime_error("fseek to data chunk failed");
                }
                break;
            }

            std::vector<uint8_t> payload(payload_sz);
            if (payload_sz > 0 && std::fread(payload.data(), 1, payload_sz, file_) != payload_sz) {
                throw std::runtime_error("truncated chunk payload");
            }
            
            uint32_t calc_crc = crc32_calculate(header, 8);
            if (payload_sz > 0) calc_crc = crc32_calculate(payload.data(), payload_sz, calc_crc);
            if (calc_crc != stored_crc) throw std::runtime_error("chunk CRC mismatch");
            
            if (id == detail::kChunkSLAC) { saw_slac = true; }
            else if (id == detail::kChunkFmt) { parse_fmt_chunk(payload.data(), payload_sz); saw_fmt = true; }
            else if (id == detail::kChunkSpat) { try { parse_spat_chunk(payload.data(), payload_sz); } catch (...) {} }
            else if (id == detail::kChunkAuto) { try { parse_auto_chunk(payload.data(), payload_sz); } catch (...) {} }
            else if (id == detail::kChunkSeek) { parse_seek_chunk(payload.data(), payload_sz); }
            else if (id == slac::core::kChunkHIRR) { parse_hirr_chunk(payload.data(), payload_sz); }
        }
        
        if (!saw_slac || !saw_fmt) throw std::runtime_error("missing required chunks (SLAC/fmt)");
        if (!saw_data) throw std::runtime_error("missing data chunk");
    }

    void parse_fmt_chunk(const uint8_t* data, size_t size) {
        if (size < 23) throw std::runtime_error("fmt chunk too small");
        size_t p = 0;
        fmt_.sampleRate = readU32LE(data + p); p += 4;
        fmt_.bitsPerSample = data[p]; p += 1;
        fmt_.channelCount = data[p]; p += 1;
        fmt_.mode = static_cast<DecorrelationMode>(data[p]); p += 1;
        p += 1; // reserved
        fmt_.totalSamples = readU64LE(data + p); p += 8;
        fmt_.frameSize = readU32LE(data + p); p += 4;
        fmt_.preset = data[p]; p += 1;
        fmt_.encoderMajor = data[p]; p += 1;
        fmt_.encoderMinor = data[p]; p += 1;
    }

    void parse_spat_chunk(const uint8_t* data, size_t size) {
        if (size < 10) return;
        size_t p = 0;
        spat_.wideness_permille = static_cast<uint16_t>(data[p]) | (static_cast<uint16_t>(data[p + 1]) << 8); p += 2;
        spat_.reverb_wet_pct = data[p++];
        spat_.preset_id = data[p++];
        spat_.flags = data[p++];
        spat_.chain_order = data[p++];
        spat_.fallback_category = data[p++];
        spat_.fallback_size = data[p++];
        spat_.fallback_decay = data[p++];
        spat_.reserved = data[p++];
    }

    void parse_auto_chunk(const uint8_t* data, size_t size) {
        auto_keyframes_.clear();
        if (!slac::core::auto_chunk_parse(data, size, auto_keyframes_)) {
            auto_keyframes_.clear();
        }
    }

    void parse_seek_chunk(const uint8_t* data, size_t size) {
        if (size < 8) throw std::runtime_error("seek chunk too small");
        size_t p = 0;
        seek_.frame_count = readU32LE(data + p); p += 4;
        seek_.frame_size = readU32LE(data + p); p += 4;
        seek_.byte_offsets.resize(seek_.frame_count);
        for (uint32_t i = 0; i < seek_.frame_count; ++i) {
            seek_.byte_offsets[i] = readU32LE(data + p); p += 4;
        }
    }

    void parse_hirr_chunk(const uint8_t* data, size_t size) {
        if (size < 12) return;
        size_t p = 0;
        
        auto read_u32 = [&]() {
            uint32_t v = data[p] | (data[p+1] << 8) | (data[p+2] << 16) | (data[p+3] << 24);
            p += 4; return v;
        };

        embedded_hrir_.sample_rate = read_u32();
        embedded_hrir_.channels = data[p++];
        embedded_hrir_.type = static_cast<slac::dsp::HRIRType>(data[p++]);
        p += 2; // reserved
        embedded_hrir_.num_samples = read_u32();

        if (embedded_hrir_.num_samples == 0) return;

        auto read_f32 = [&]() {
            uint32_t u = data[p] | (data[p+1] << 8) | (data[p+2] << 16) | (data[p+3] << 24);
            p += 4;
            float v; std::memcpy(&v, &u, 4);
            return v;
        };

        embedded_hrir_.ll.resize(embedded_hrir_.num_samples);
        embedded_hrir_.rr.resize(embedded_hrir_.num_samples);
        for (uint32_t i = 0; i < embedded_hrir_.num_samples; ++i) embedded_hrir_.ll[i] = read_f32();
        for (uint32_t i = 0; i < embedded_hrir_.num_samples; ++i) embedded_hrir_.rr[i] = read_f32();

        if (embedded_hrir_.channels >= 4) {
            embedded_hrir_.lr.resize(embedded_hrir_.num_samples);
            embedded_hrir_.rl.resize(embedded_hrir_.num_samples);
            for (uint32_t i = 0; i < embedded_hrir_.num_samples; ++i) embedded_hrir_.lr[i] = read_f32();
            for (uint32_t i = 0; i < embedded_hrir_.num_samples; ++i) embedded_hrir_.rl[i] = read_f32();
        }
    }


    bool decode_next_frame() {
        if (!file_) return false;
        uint8_t header[11];
        if (std::fread(header, 1, 11, file_) != 11) return false;
        if (header[0] != FRAME_SYNC_0 || header[1] != FRAME_SYNC_1) return false;
        uint16_t frame_samples = 0;
        std::memcpy(&frame_samples, header + 2, 2);
        uint32_t data_size = 0;
        std::memcpy(&data_size, header + 5, 4);
        size_t total_size = data_size + 2;
        std::vector<uint8_t> frame_data(11 + total_size);
        std::memcpy(frame_data.data(), header, 11);
        if (std::fread(frame_data.data() + 11, 1, total_size, file_) != total_size) return false;
        auto result = decode_frame(frame_data.data(), frame_data.size(), fmt_.bitsPerSample);
        if (!result.valid) return false;

        size_t channels = fmt_.channelCount;
        current_frame_.resize(channels);
        current_frame_size_ = frame_samples;
        sample_pos_in_frame_ = 0;

        if (channels == 2 && result.num_streams == 2) {
            std::vector<int32_t> frame_l, frame_r;
            stereoDecode(result.streams[0], result.streams[1], result.stereo_mode, frame_l, frame_r);
            current_frame_[0] = std::move(frame_l);
            current_frame_[1] = std::move(frame_r);
        } else if (channels == 1 && result.num_streams == 1) {
            current_frame_[0] = std::move(result.streams[0]);
        } else if (result.num_streams == 1 && channels == 2) {
            current_frame_[0] = result.streams[0];
            current_frame_[1] = result.streams[0];
        } else {
            for (size_t c = 0; c < channels && c < result.streams.size(); ++c) {
                current_frame_[c] = result.streams[c];
            }
        }
        ++current_frame_index_;
        return true;
    }
};

} // namespace slac::rt