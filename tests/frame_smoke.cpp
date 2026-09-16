#include <slac/core/container.hpp>
#include <slac/core/frame.hpp>

#include <cmath>
#include <iostream>
#include <vector>

using namespace slac;

static bool check(bool cond, const char* label) {
    if (cond) { std::cout << "[OK] " << label << "\n"; }
    else      { std::cerr << "[FAIL] " << label << "\n"; }
    return cond;
}

static bool vec_equal(const std::vector<int32_t>& a, const std::vector<int32_t>& b) {
    return a == b;
}

int main() {
    constexpr int bits = 16;

    // ── 1. Single frame encode/decode ───────────────────────
    {
        std::vector<int32_t> mono(4096);
        for (size_t i = 0; i < mono.size(); ++i) {
            mono[i] = static_cast<int32_t>(
                10000.0 * std::sin(2.0 * 3.14159 * 440.0 * i / 44100.0));
        }

        std::vector<std::vector<int32_t>> streams = {mono};
        auto frame = encode_frame(streams, bits, 8, 15);

        auto result = decode_frame(frame.data(), frame.size(), bits);

        check(result.valid, "Single frame decodes");
        check(result.frame_samples == 4096, "Frame has 4096 samples");
        check(result.num_streams == 1, "Frame has 1 stream");
        check(vec_equal(mono, result.streams[0]), "Frame round-trip bit-exact");
    }

    // ── 2. CRC detects corruption ───────────────────────────
    {
        std::vector<int32_t> mono(256, 1000);
        std::vector<std::vector<int32_t>> streams = {mono};
        auto frame = encode_frame(streams, bits, 4, 15);

        // Corrupt a byte in the data region.
        frame[15] ^= 0xFF;

        auto result = decode_frame(frame.data(), frame.size(), bits);
        check(!result.valid, "Corrupted frame rejected");
        check(result.error == FrameDecodeResult::BAD_DATA_CRC,
              "Error is BAD_DATA_CRC");
    }

    // ── 3. Sync detection ───────────────────────────────────
    {
        uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                             0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
        auto result = decode_frame(garbage, sizeof(garbage), bits);
        check(!result.valid, "Garbage rejected");
        check(result.error == FrameDecodeResult::BAD_SYNC, "Error is BAD_SYNC");
    }

    // ── 4. Full container with frames ───────────────────────
    {
        // Generate stereo signal: 3 frames worth.
        size_t total = 4096 * 3 + 1000; // not evenly divisible
        std::vector<int32_t> left(total), right(total);

        for (size_t i = 0; i < total; ++i) {
            double t = static_cast<double>(i) / 48000.0;
            left[i]  = static_cast<int32_t>(8000.0 * std::sin(2.0 * 3.14159 * 440.0 * t));
            right[i] = static_cast<int32_t>(6000.0 * std::sin(2.0 * 3.14159 * 550.0 * t));
        }

        SlacFormat fmt;
        fmt.sampleRate = 48000;
        fmt.bitsPerSample = bits;
        fmt.channelCount = 2;
        fmt.frameSize = 4096;

        std::vector<std::vector<int32_t>> input = {left, right};

        // Encode.
        auto file = encodeSlacFile(input, fmt, nullptr, 4, 15);

        // Decode.
        SlacFormat dec_fmt;
        SeekTable dec_seek;
        auto output = decodeSlacFile(file, &dec_fmt, nullptr, &dec_seek);

        check(dec_fmt.channelCount == 2, "Decoded 2 channels");
        check(dec_fmt.frameSize == 4096, "Frame size preserved");
        check(dec_seek.frame_count == 4, "Seek table has 4 frames (3 full + 1 partial)");
        check(output.size() == 2, "Output has 2 channels");
        check(output[0].size() == total, "Output[0] size matches");
        check(output[1].size() == total, "Output[1] size matches");
        check(vec_equal(left, output[0]), "Left channel bit-exact");
        check(vec_equal(right, output[1]), "Right channel bit-exact");
    }

    // ── 5. Seek table correctness ───────────────────────────
    {
        size_t total = 4096 * 2;
        std::vector<int32_t> mono(total, 500);

        SlacFormat fmt;
        fmt.sampleRate = 44100;
        fmt.bitsPerSample = bits;
        fmt.channelCount = 1;
        fmt.frameSize = 4096;

        std::vector<std::vector<int32_t>> input = {mono};
        auto file = encodeSlacFile(input, fmt);

        SeekTable seek;
        decodeSlacFile(file, nullptr, nullptr, &seek);

        check(seek.frame_count == 2, "2 frames for 8192 samples");
        check(seek.frame_size == 4096, "Frame size is 4096");
        check(seek.byte_offsets.size() == 2, "2 byte offsets");
        check(seek.byte_offsets[0] == 0, "First frame at offset 0");
        check(seek.byte_offsets[1] > 0, "Second frame at nonzero offset");
    }

    // ── 6. Mono container ───────────────────────────────────
    {
        size_t total = 8000;
        std::vector<int32_t> mono(total);
        for (size_t i = 0; i < total; ++i) {
            mono[i] = static_cast<int32_t>(i % 1000 - 500);
        }

        SlacFormat fmt;
        fmt.sampleRate = 44100;
        fmt.bitsPerSample = bits;
        fmt.channelCount = 1;
        fmt.frameSize = 4096;

        std::vector<std::vector<int32_t>> input = {mono};
        auto file = encodeSlacFile(input, fmt);
        auto output = decodeSlacFile(file);

        check(output.size() == 1, "Mono output has 1 channel");
        check(vec_equal(mono, output[0]), "Mono round-trip bit-exact");
    }

    // ── 7. Wasted bits detection ────────────────────────────
    {
        // Samples with 4 trailing zero bits (e.g., 12-bit in 16-bit container).
        std::vector<int32_t> mono(256);
        for (size_t i = 0; i < mono.size(); ++i) {
            mono[i] = static_cast<int32_t>((i % 100 - 50) * 16); // ×16 = 4 trailing zeros
        }

        std::vector<std::vector<int32_t>> streams = {mono};
        auto frame = encode_frame(streams, bits, 4, 15);
        auto result = decode_frame(frame.data(), frame.size(), bits);

        check(result.valid, "Wasted-bits frame decodes");
        check(vec_equal(mono, result.streams[0]), "Wasted-bits round-trip bit-exact");
    }

    // ── 8. Partitioned Rice with transients ─────────────────
    {
        // Signal with a transient in the middle — partitioning helps.
        std::vector<int32_t> mono(4096, 100);
        for (size_t i = 2000; i < 2100; ++i) {
            mono[i] = 30000; // transient spike
        }

        std::vector<std::vector<int32_t>> streams = {mono};
        auto frame = encode_frame(streams, bits, 4, 15);
        auto result = decode_frame(frame.data(), frame.size(), bits);

        check(result.valid, "Transient frame decodes");
        check(vec_equal(mono, result.streams[0]), "Transient round-trip bit-exact");
    }

    // ── 9. Best-order LPC improves compression ──────────────
    {
        // Pure sine wave: low-order LPC should be optimal.
        std::vector<int32_t> sine(4096);
        for (size_t i = 0; i < sine.size(); ++i) {
            sine[i] = static_cast<int32_t>(
                20000.0 * std::sin(2.0 * 3.14159 * 440.0 * i / 44100.0));
        }

        std::vector<std::vector<int32_t>> streams = {sine};
        auto frame = encode_frame(streams, bits, 12, 15); // maxOrder=12
        auto result = decode_frame(frame.data(), frame.size(), bits);

        check(result.valid, "Best-order sine frame decodes");
        check(vec_equal(sine, result.streams[0]), "Best-order sine round-trip bit-exact");
    }

    // ── 10. White noise: order 0 or low order optimal ───────
    {
        std::vector<int32_t> noise(4096);
        uint32_t seed = 12345;
        for (size_t i = 0; i < noise.size(); ++i) {
            seed = seed * 1103515245 + 12345;
            noise[i] = static_cast<int32_t>((seed >> 16) & 0x7FFF) - 16384;
        }

        std::vector<std::vector<int32_t>> streams = {noise};
        auto frame = encode_frame(streams, bits, 12, 15);
        auto result = decode_frame(frame.data(), frame.size(), bits);

        check(result.valid, "Best-order noise frame decodes");
        check(vec_equal(noise, result.streams[0]), "Best-order noise round-trip bit-exact");
    }

    std::cout << "\n--- Frame structure tests PASSED ---\n";
    return 0;
}
