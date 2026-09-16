#pragma once

#include "block.hpp"
#include "crc16.hpp"
#include "ms.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace slac {

static constexpr uint8_t  FRAME_SYNC_0       = 0xFF;
static constexpr uint8_t  FRAME_SYNC_1       = 0x53;
static constexpr size_t   FRAME_HEADER_SIZE  = 11;
static constexpr size_t   FRAME_TRAILER_SIZE = 2;
static constexpr uint32_t DEFAULT_FRAME_SIZE = 4096;

// ──────────────────────────────────────────────────────────────
// Frame layout:
//
//   [0-1]    sync:          0xFF 0x53
//   [2-3]    frame_samples: uint16 LE
//   [4]      packed:        bits 0-1 = stereo_mode, bits 2-7 = num_streams
//   [5-8]    data_size:     uint32 LE
//   [9-10]   header_crc:    uint16 LE  (CRC16 of bytes 0-8)
//   [11..]   stream_data:   data_size bytes
//   [end-1]  data_crc:      uint16 LE  (CRC16 of stream_data)
// ──────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────
// Encode a single frame
// ──────────────────────────────────────────────────────────────

inline std::vector<uint8_t> encode_frame(
    const std::vector<std::vector<int32_t>>& frame_streams,
    int bits_per_sample,
    int lpc_order,
    int lpc_shift,
    StereoMode stereo_mode = StereoMode::MidSide,
    bool use_range_coding = false,
    int stream1_bits = -1)
{
    if (frame_streams.empty()) {
        throw std::invalid_argument("encode_frame: no streams");
    }

    uint16_t frame_samples = static_cast<uint16_t>(frame_streams[0].size());
    uint8_t  num_streams   = static_cast<uint8_t>(frame_streams.size());

    std::vector<uint8_t> stream_data;
    for (size_t s = 0; s < frame_streams.size(); ++s) {
        int bps = bits_per_sample;
        if (s == 1 && stream1_bits > 0) bps = stream1_bits;
        auto block = encodeBlockMono(frame_streams[s], bps,
                                     lpc_order, lpc_shift, use_range_coding);
        stream_data.insert(stream_data.end(), block.begin(), block.end());
    }

    uint32_t data_size = static_cast<uint32_t>(stream_data.size());

    std::vector<uint8_t> frame;
    frame.reserve(FRAME_HEADER_SIZE + data_size + FRAME_TRAILER_SIZE);

    frame.push_back(FRAME_SYNC_0);
    frame.push_back(FRAME_SYNC_1);

    frame.push_back(static_cast<uint8_t>(frame_samples & 0xFF));
    frame.push_back(static_cast<uint8_t>((frame_samples >> 8) & 0xFF));

    uint8_t mode_packed = static_cast<uint8_t>(
        (static_cast<uint8_t>(stereo_mode) & 0x03u) |
        ((num_streams & 0x3Fu) << 2));
    frame.push_back(mode_packed);

    frame.push_back(static_cast<uint8_t>(data_size & 0xFF));
    frame.push_back(static_cast<uint8_t>((data_size >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>((data_size >> 16) & 0xFF));
    frame.push_back(static_cast<uint8_t>((data_size >> 24) & 0xFF));

    uint16_t hdr_crc = crc16(frame.data(), 9);
    frame.push_back(static_cast<uint8_t>(hdr_crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((hdr_crc >> 8) & 0xFF));

    frame.insert(frame.end(), stream_data.begin(), stream_data.end());

    uint16_t data_crc = crc16(stream_data.data(), stream_data.size());
    frame.push_back(static_cast<uint8_t>(data_crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((data_crc >> 8) & 0xFF));

    return frame;
}

// ──────────────────────────────────────────────────────────────
// Decode a single frame
// ──────────────────────────────────────────────────────────────

struct FrameDecodeResult {
    bool valid = false;
    uint16_t frame_samples = 0;
    uint8_t num_streams = 0;
    StereoMode stereo_mode = StereoMode::MidSide;
    std::vector<std::vector<int32_t>> streams;
    size_t bytes_consumed = 0;

    enum Error {
        NONE = 0,
        TOO_SHORT,
        BAD_SYNC,
        BAD_HEADER_CRC,
        BAD_DATA_CRC,
        DECODE_ERROR,
    };
    Error error = NONE;
};

inline FrameDecodeResult decode_frame(
    const uint8_t* data,
    size_t size,
    int bits_per_sample
) {
    (void)bits_per_sample; // lido internamente pelo decodeBlockMono

    FrameDecodeResult result;

    // Minimum: header(11) + trailer(2).
    if (size < FRAME_HEADER_SIZE + FRAME_TRAILER_SIZE) {
        result.error = FrameDecodeResult::TOO_SHORT;
        return result;
    }

    // Sync check.
    if (data[0] != FRAME_SYNC_0 || data[1] != FRAME_SYNC_1) {
        result.error = FrameDecodeResult::BAD_SYNC;
        return result;
    }

    // Header fields.
    result.frame_samples = static_cast<uint16_t>(data[2]) |
                           (static_cast<uint16_t>(data[3]) << 8);

    // Num streams + stereo mode packed into byte [4].
    uint8_t mode_packed = data[4];
    result.num_streams = (mode_packed >> 2) & 0x3Fu;
    result.stereo_mode = static_cast<StereoMode>(mode_packed & 0x03u);

    uint32_t data_size = static_cast<uint32_t>(data[5]) |
                         (static_cast<uint32_t>(data[6]) << 8) |
                         (static_cast<uint32_t>(data[7]) << 16) |
                         (static_cast<uint32_t>(data[8]) << 24);

    // Header CRC.
    uint16_t hdr_crc = crc16(data, 9);
    uint16_t stored_hdr_crc = static_cast<uint16_t>(data[9]) |
                              (static_cast<uint16_t>(data[10]) << 8);
    if (hdr_crc != stored_hdr_crc) {
        result.error = FrameDecodeResult::BAD_HEADER_CRC;
        return result;
    }

    // Total frame size.
    size_t total = FRAME_HEADER_SIZE + data_size + FRAME_TRAILER_SIZE;
    if (size < total) {
        result.error = FrameDecodeResult::TOO_SHORT;
        return result;
    }

    // Data CRC.
    const uint8_t* stream_data = data + FRAME_HEADER_SIZE;
    uint16_t data_crc = crc16(stream_data, data_size);
    uint16_t stored_data_crc =
        static_cast<uint16_t>(data[FRAME_HEADER_SIZE + data_size]) |
        (static_cast<uint16_t>(data[FRAME_HEADER_SIZE + data_size + 1]) << 8);

    if (data_crc != stored_data_crc) {
        result.error = FrameDecodeResult::BAD_DATA_CRC;
        return result;
    }

    // Decode each stream.
    size_t offset = 0;
    result.streams.resize(result.num_streams);

    for (int s = 0; s < result.num_streams; ++s) {
        if (offset >= data_size) {
            result.error = FrameDecodeResult::DECODE_ERROR;
            return result;
        }

        size_t consumed = 0;
        try {
            result.streams[s] = decodeBlockMono(
                stream_data + offset,
                data_size - offset,
                &consumed
            );
        } catch (...) {
            result.error = FrameDecodeResult::DECODE_ERROR;
            return result;
        }

        offset += consumed;
    }

    result.valid = true;
    result.bytes_consumed = total;
    return result;
}

} // namespace slac
