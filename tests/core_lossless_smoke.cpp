#include <slac/core/block.hpp>
#include <slac/core/ms.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace slac;

static bool checkEqual(
    const std::vector<int32_t>& expected,
    const std::vector<int32_t>& actual,
    const char* label
) {
    if (expected.size() != actual.size()) {
        std::cerr << "[" << label << "] size mismatch: expected "
                  << expected.size() << ", got " << actual.size() << "\n";
        return false;
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual[i]) {
            std::cerr << "[" << label << "] mismatch at index " << i
                      << ": expected " << expected[i]
                      << ", got " << actual[i] << "\n";
            return false;
        }
    }

    return true;
}

int main() {
    constexpr double pi = 3.14159265358979323846;

    // --------------------------------------------------
    // Mono test
    // --------------------------------------------------
    std::vector<int32_t> mono;
    mono.reserve(4096);

    for (int i = 0; i < 4096; ++i) {
        double t = static_cast<double>(i) / 44100.0;

        double v =
            12000.0 * std::sin(2.0 * pi * 440.0 * t) +
            5000.0 * std::sin(2.0 * pi * 1000.0 * t);

        mono.push_back(static_cast<int32_t>(std::lround(v)));
    }

    std::vector<uint8_t> monoEncoded = encodeBlockMono(mono, 16, 8, 15);
    std::vector<int32_t> monoDecoded = decodeBlockMono(monoEncoded);

    if (!checkEqual(mono, monoDecoded, "mono")) {
        return 1;
    }

    std::cout << "Mono LPC/Rice bit-exact: OK\n";

    // --------------------------------------------------
    // Stereo M/S test
    // --------------------------------------------------
    std::vector<int32_t> left;
    std::vector<int32_t> right;

    left.reserve(4096);
    right.reserve(4096);

    for (int i = 0; i < 4096; ++i) {
        double t = static_cast<double>(i) / 44100.0;

        double l =
            10000.0 * std::sin(2.0 * pi * 300.0 * t) +
            3000.0 * std::sin(2.0 * pi * 900.0 * t);

        double r =
            8000.0 * std::sin(2.0 * pi * 320.0 * t) +
            2500.0 * std::sin(2.0 * pi * 950.0 * t);

        left.push_back(static_cast<int32_t>(std::lround(l)));
        right.push_back(static_cast<int32_t>(std::lround(r)));
    }

    std::vector<int32_t> mid;
    std::vector<int32_t> side;

    midSideEncode(left, right, mid, side);

    // For 16-bit L/R:
    // - mid usually fits in 16 bits;
    // - side may require 17 bits.
    std::vector<uint8_t> midEncoded = encodeBlockMono(mid, 16, 4, 15);
    std::vector<uint8_t> sideEncoded = encodeBlockMono(side, 17, 4, 15);

    std::vector<int32_t> midDecoded = decodeBlockMono(midEncoded);
    std::vector<int32_t> sideDecoded = decodeBlockMono(sideEncoded);

    if (!checkEqual(mid, midDecoded, "mid")) {
        return 1;
    }

    if (!checkEqual(side, sideDecoded, "side")) {
        return 1;
    }

    std::vector<int32_t> leftDecoded;
    std::vector<int32_t> rightDecoded;

    midSideDecode(midDecoded, sideDecoded, leftDecoded, rightDecoded);

    if (!checkEqual(left, leftDecoded, "left")) {
        return 1;
    }

    if (!checkEqual(right, rightDecoded, "right")) {
        return 1;
    }

    std::cout << "M/S fixed bit-exact: OK\n";
    std::cout << "SLAC Core MVP smoke test: OK\n";

    return 0;
}
