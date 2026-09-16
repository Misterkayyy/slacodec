#include <slac/core/container.hpp>
#include <slac/dsp/processor.hpp>

#include <cmath>
#include <iostream>
#include <vector>

using namespace slac;

static bool checkEqual(
    const std::vector<int32_t>& expected,
    const std::vector<int32_t>& actual,
    const char* label
) {
    if (expected.size() != actual.size()) {
        std::cerr << "[" << label << "] size mismatch\n";
        return false;
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual[i]) {
            std::cerr << "[" << label << "] mismatch at " << i << "\n";
            return false;
        }
    }
    return true;
}

int main() {
    constexpr double pi = 3.14159265358979323846;

    // Generate mono test signal.
    std::vector<int32_t> mono;
    mono.reserve(4096);
    for (int i = 0; i < 4096; ++i) {
        double t = static_cast<double>(i) / 44100.0;
        double v = 12000.0 * std::sin(2.0 * pi * 440.0 * t) +
                   5000.0 * std::sin(2.0 * pi * 1000.0 * t);
        mono.push_back(static_cast<int32_t>(std::lround(v)));
    }

    // ── 1. Encode without spat (SLAC Core pure) ─────────────
    SlacFormat fmt;
    fmt.sampleRate = 44100;
    fmt.bitsPerSample = 16;
    fmt.channelCount = 1;
    fmt.frameSize = 4096;

    std::vector<std::vector<int32_t>> input = {mono};
    std::vector<uint8_t> filePure = encodeSlacFile(input, fmt);

    SlacFormat decFmt;
    SpatMetadata decSpat;
    auto outPure = decodeSlacFile(filePure, &decFmt, &decSpat);

    if (!checkEqual(mono, outPure[0], "pure mono")) return 1;
    std::cout << "[OK] Decode without spat chunk.\n";

    // ── 2. Encode WITH spat (SLAC Spatial metadata) ─────────
    SpatMetadata myMix;
    myMix.wideness_permille = 1250;
    myMix.reverb_wet_pct = 15;
    myMix.preset_id = 2;
    myMix.flags = 0x02;

    std::vector<uint8_t> fileSpat = encodeSlacFile(input, fmt, &myMix);

    SlacFormat decFmt2;
    SpatMetadata decSpat2;
    auto outSpat = decodeSlacFile(fileSpat, &decFmt2, &decSpat2);

    if (!checkEqual(mono, outSpat[0], "spat mono audio")) return 1;
    std::cout << "[OK] Audio decoded perfectly despite spat chunk (SLAC Core compliant).\n";

    if (decSpat2.wideness_permille != 1250 ||
        decSpat2.reverb_wet_pct != 15 ||
        decSpat2.preset_id != 2) {
        std::cerr << "[FAIL] Spat metadata mismatch!\n";
        return 1;
    }
    std::cout << "[OK] Spat metadata read correctly: Wideness="
              << decSpat2.wideness_permille / 10.0
              << "%, Wet=" << static_cast<int>(decSpat2.reverb_wet_pct)
              << "%, Preset=" << static_cast<int>(decSpat2.preset_id) << "\n";

    // ── 3. CRC32 corruption detection ───────────────────────
    std::vector<uint8_t> corruptedFile = fileSpat;
    size_t corruptPos = corruptedFile.size() / 2;
    corruptedFile[corruptPos] ^= 0xFF;

    bool caughtCrcError = false;
    try {
        // strict_crc = true (5th argument; 4th is SeekTable*)
   	 decodeSlacFile(corruptedFile, &decFmt, &decSpat, nullptr, nullptr, true);
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()).find("CRC") != std::string::npos) {
            caughtCrcError = true;
        }
    }

    if (!caughtCrcError) {
        std::cerr << "[FAIL] Decoder did not catch CRC32 corruption!\n";
        return 1;
    }
    std::cout << "[OK] CRC32 successfully caught file corruption.\n";

    // ── 4. Seek table present ───────────────────────────────
    SeekTable seek;
    decodeSlacFile(filePure, nullptr, nullptr, &seek);

    if (seek.frame_count == 0) {
        std::cerr << "[FAIL] Seek table empty!\n";
        return 1;
    }
    std::cout << "[OK] Seek table present: " << seek.frame_count
              << " frame(s), size=" << seek.frame_size << "\n";

    std::cout << "\n--- SLAC Phase 1 MVP (Core + Spat + CRC + Frames) PASSED ---\n";
    return 0;
}
