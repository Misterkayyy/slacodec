#include "slac/core/auto_chunk.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>

using namespace slac::core;

static int g_failures = 0;

static void check(bool condition, const char* msg) {
    if (!condition) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    } else {
        std::cout << "[PASS] " << msg << "\n";
    }
}

static void test_roundtrip() {
    std::vector<AutoKeyframe> kfs = {
        {0, 0, 0.0f},
        {44100, 0, 500.0f},
        {44100, 1, 50.0f},
        {88200, 0, 1000.0f},
        {88200, 1, 100.0f},
    };

    std::vector<uint8_t> buf;
    check(auto_chunk_serialize(kfs, buf), "serialize roundtrip");
    check(buf.size() == 8 + 5 * 9, "roundtrip size");

    std::vector<AutoKeyframe> parsed;
    check(auto_chunk_parse(buf.data(), buf.size(), parsed), "parse roundtrip");
    check(parsed.size() == kfs.size(), "roundtrip count");

    bool values_match = true;
    for (size_t i = 0; i < kfs.size(); ++i) {
        if (parsed[i].sample_offset != kfs[i].sample_offset ||
            parsed[i].param_id != kfs[i].param_id ||
            parsed[i].value != kfs[i].value) {
            values_match = false;
            break;
        }
    }
    check(values_match, "roundtrip values match");
}

static void test_empty() {
    std::vector<AutoKeyframe> kfs;
    std::vector<uint8_t> buf;
    check(auto_chunk_serialize(kfs, buf), "serialize empty");
    check(buf.size() == 8, "empty size");

    std::vector<AutoKeyframe> parsed;
    check(auto_chunk_parse(buf.data(), buf.size(), parsed), "parse empty");
    check(parsed.empty(), "empty parsed is empty");
}

static void test_truncated() {
    std::vector<AutoKeyframe> kfs = {{0, 0, 0.0f}};
    std::vector<uint8_t> buf;
    check(auto_chunk_serialize(kfs, buf), "serialize for truncated test");

    std::vector<AutoKeyframe> parsed;
    check(!auto_chunk_parse(buf.data(), buf.size() - 1, parsed), "reject truncated by 1 byte");
    check(!auto_chunk_parse(buf.data(), 4, parsed), "reject incomplete header");
}

static void test_invalid_version() {
    std::vector<AutoKeyframe> kfs = {{0, 0, 0.0f}};
    std::vector<uint8_t> buf;
    check(auto_chunk_serialize(kfs, buf), "serialize for version test");

    buf[0] = 2; // version = 2
    std::vector<AutoKeyframe> parsed;
    check(!auto_chunk_parse(buf.data(), buf.size(), parsed), "reject invalid version");
}

static void test_invalid_reserved() {
    std::vector<AutoKeyframe> kfs = {{0, 0, 0.0f}};
    std::vector<uint8_t> buf;
    check(auto_chunk_serialize(kfs, buf), "serialize for reserved test");

    buf[2] = 1; // reserved != 0
    std::vector<AutoKeyframe> parsed;
    check(!auto_chunk_parse(buf.data(), buf.size(), parsed), "reject non-zero reserved");
}

static void test_invalid_param_id() {
    std::vector<AutoKeyframe> kfs = {{0, 99, 0.0f}};
    std::vector<uint8_t> buf;
    check(!auto_chunk_serialize(kfs, buf), "serialize rejects invalid param_id");

    std::vector<uint8_t> bad(8 + 9, 0);
    bad[0] = 1;
    bad[8] = 99;
    std::vector<AutoKeyframe> parsed;
    check(!auto_chunk_parse(bad.data(), bad.size(), parsed), "parse rejects invalid param_id");
}

static void test_nan_inf() {
    std::vector<AutoKeyframe> kfs = {{0, 0, std::nanf("")}};
    std::vector<uint8_t> buf;
    check(!auto_chunk_serialize(kfs, buf), "serialize rejects NaN");

    kfs[0].value = std::numeric_limits<float>::infinity();
    check(!auto_chunk_serialize(kfs, buf), "serialize rejects Inf");

    std::vector<uint8_t> bad(8 + 9, 0);
    bad[0] = 1;
    bad[8] = 0;
    float nan_val = std::nanf("");
    std::memcpy(bad.data() + 8 + 5, &nan_val, 4);
    std::vector<AutoKeyframe> parsed;
    check(!auto_chunk_parse(bad.data(), bad.size(), parsed), "parse rejects NaN");
}

static void test_unsorted() {
    std::vector<AutoKeyframe> kfs = {
        {100, 0, 0.0f},
        {50, 0, 0.0f},
    };
    std::vector<uint8_t> buf;
    check(!auto_chunk_serialize(kfs, buf), "serialize rejects unsorted");
}

static void test_duplicate_offset_param() {
    std::vector<AutoKeyframe> kfs = {
        {100, 0, 10.0f},
        {100, 0, 20.0f},
    };
    std::vector<uint8_t> buf;
    check(!auto_chunk_serialize(kfs, buf), "serialize rejects duplicate (offset, param_id)");
}

int main() {
    test_roundtrip();
    test_empty();
    test_truncated();
    test_invalid_version();
    test_invalid_reserved();
    test_invalid_param_id();
    test_nan_inf();
    test_unsorted();
    test_duplicate_offset_param();

    if (g_failures == 0) {
        std::cout << "\n=== ALL AUTO CHUNK SMOKE TESTS PASSED ===\n";
        return 0;
    } else {
        std::cerr << "\n=== " << g_failures << " TEST(S) FAILED ===\n";
        return 1;
    }
}
