// src/slac/cli/main.cpp
//
// SLACodec CLI — Spatial Lossless Audio Codec
// Subcommands: encode, decode, info, verify, gen
// Presets: fast | balanced | high | extreme
// Cadeia espacial: widening -> HRIR -> reverb (IR ou FDN) -> headroom
// Range coder: EXPERIMENTAL, opt-in via --entropy-range

#include <slac/core/container.hpp>
#include <slac/core/wav_writer.hpp>
#include <slac/dsp/processor.hpp>
#include <slac/dsp/reverb_engine.hpp>
#include <slac/dsp/wav_loader.hpp>
#include <slac/dsp/analyzer.hpp>
#include <slac/dsp/spatial_chain.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static constexpr const char* SLAC_VERSION = "0.6.0-headroom";

// ──────────────────────────────────────────────────────────────
// Argument helpers
// ──────────────────────────────────────────────────────────────

static bool has_flag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

static std::string get_opt(int argc, char** argv, const char* name,
                           const std::string& default_val) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0 && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return default_val;
}

static int get_opt_int(int argc, char** argv, const char* name, int default_val) {
    std::string s = get_opt(argc, argv, name, std::to_string(default_val));
    try { return std::stoi(s); }
    catch (...) { return default_val; }
}

// ──────────────────────────────────────────────────────────────
// File I/O helpers
// ──────────────────────────────────────────────────────────────

static bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    in.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    out.resize(size);
    in.read(reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(size));
    return true;
}

static bool write_file(const std::string& path,
                       const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return true;
}

// Conversao float->PCM compartilhada por encode e verify (idempotente).
static int32_t float_to_pcm(float v) {
    float s = v * 32768.0f;
    if (s >  32767.0f) s =  32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return static_cast<int32_t>(s);
}

// ──────────────────────────────────────────────────────────────
// Encoder presets (§3.5)
// ──────────────────────────────────────────────────────────────

struct EncoderPreset {
    const char* name;
    uint32_t frame_size;
    int max_lpc_order;
    int lpc_shift;
    bool adaptive;
    bool use_range_coding;
};

static EncoderPreset get_preset(const std::string& name) {
    if (name == "fast")
        return {"fast",     2048,  4, 15, false, false};
    if (name == "balanced")
        return {"balanced", 4096,  8, 15, true,  false};
    if (name == "high")
        return {"high",     4096, 12, 15, true,  false};
    if (name == "extreme")
        return {"extreme",  8192, 16, 15, true,  false};
    return {"balanced", 4096, 8, 15, true, false};
}

// ──────────────────────────────────────────────────────────────
// Usage / version
// ──────────────────────────────────────────────────────────────

static void print_usage() {
    std::cout <<
R"(SLACodec CLI v)" << SLAC_VERSION << R"( — Spatial Lossless Audio Codec

Usage:
  slacodec-cli encode <input.wav> <output.slac> [options]
  slacodec-cli decode <input.slac> <output.wav> [options]
  slacodec-cli info   <input.slac>
  slacodec-cli verify <original.wav> <input.slac>
  slacodec-cli gen    <name> <out.wav>

Gen names (corpus sintetico deterministico):
  silence | sine | noise | transient | wasted | mono | stereo-wide | stereo-asym

Encode options:
  --preset-encode <name>   Encoder preset: fast|balanced|high|extreme
  --adaptive               Force adaptive stereo decorrelation
  --fixed-ms               Force fixed M/S (disable adaptive)
  --frame-size <n>         Samples per frame (overrides preset)
  --lpc-order <n>          Max LPC order (overrides preset)
  --lpc-shift <n>          LPC coefficient shift (default: 15)
  --entropy-range          Use EXPERIMENTAL range coder (opt-in)

  --auto-spatial           Auto-derive spatial params from audio analysis
  --wideness <0-150>       Stereo wideness % (default: 100)
  --reverb-wet <0-100>     Reverb wet % (default: 0)
  --preset <0-255>         Reverb preset ID (default: 0)

Decode options:
  --spatial                Apply spatial DSP chain (widening -> HRIR -> reverb)
  --hrir <path>            Apply true-stereo HRIR (spatial signature) before reverb
  --ir <path>              Path to convolution reverb IR WAV
  --ir-dir <path>          Directory containing reverb IR WAVs (default: ./ir)
  --fixed-reverb           Disable content-adaptive wet ducking
  --limit-mode <m>         normalize | loudness | limit | none (default: normalize)
  --hrir-echo-trim <0-100> Atenua ecos da HRIR apos o tap direto (%; 100 = off)
  --no-limit               Disable headroom management entirely

Verify:
  Decodes <input.slac> and compares sample-by-sample against
  <original.wav>. Prints VERIFY OK (bit-exact) plus SHA-256 hash
  status, or VERIFY FAIL with mismatch count and first position.

  -h, --help               Show this help
  -v, --version            Show version
)";
}

static void print_version() {
    std::cout << "slacodec-cli " << SLAC_VERSION << "\n";
    std::cout << "SLAC container — frames, adaptive decorrelation, presets,\n";
    std::cout << "hash chunk (SHA-256), verify, golden corpus,\n";
    std::cout << "spatial chain with unity-gain HRIR + headroom management\n";
}

// ──────────────────────────────────────────────────────────────
// spat fallback descriptors (§2.6)
// ──────────────────────────────────────────────────────────────

static void fill_fallback(slac::SpatMetadata& spat, uint8_t preset_id) {
    switch (preset_id) {
        case 1: spat.fallback_category=1; spat.fallback_size=1; spat.fallback_decay=2; break;
        case 2: spat.fallback_category=2; spat.fallback_size=2; spat.fallback_decay=3; break;
        case 3: spat.fallback_category=3; spat.fallback_size=3; spat.fallback_decay=4; break;
        case 4: spat.fallback_category=4; spat.fallback_size=0; spat.fallback_decay=3; break;
        case 5: spat.fallback_category=5; spat.fallback_size=1; spat.fallback_decay=2; break;
        case 6: spat.fallback_category=6; spat.fallback_size=4; spat.fallback_decay=1; break;
        default: break;
    }
}

// ──────────────────────────────────────────────────────────────
// ENCODE
// ──────────────────────────────────────────────────────────────

static int cmd_encode(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: slacodec-cli encode <input.wav> <output.slac> [options]\n";
        return 1;
    }

    std::string input_path  = argv[2];
    std::string output_path = argv[3];

    std::string preset_name = get_opt(argc, argv, "--preset-encode", "balanced");
    EncoderPreset preset = get_preset(preset_name);

    bool auto_spatial = has_flag(argc, argv, "--auto-spatial");

    int wideness   = 100;
    int reverb_wet = 0;
    int preset_id  = 0;

    int frame_size = get_opt_int(argc, argv, "--frame-size",
                                 static_cast<int>(preset.frame_size));
    int lpc_order  = get_opt_int(argc, argv, "--lpc-order", preset.max_lpc_order);
    int lpc_shift  = get_opt_int(argc, argv, "--lpc-shift", preset.lpc_shift);

    bool force_adaptive = has_flag(argc, argv, "--adaptive");
    bool force_fixed    = has_flag(argc, argv, "--fixed-ms");
    bool adaptive = force_adaptive || (!force_fixed && preset.adaptive);
    if (force_fixed) adaptive = false;

    bool entropy_range = has_flag(argc, argv, "--entropy-range") ||
                         preset.use_range_coding;

    slac::dsp::WavData wav;
    try {
        wav = slac::dsp::load_wav(input_path);
    } catch (const std::exception& e) {
        std::cerr << "Error reading input: " << e.what() << "\n";
        return 1;
    }

    if (wav.num_channels < 1 || wav.num_channels > 2) {
        std::cerr << "Error: only mono and stereo supported (got "
                  << static_cast<int>(wav.num_channels) << " ch)\n";
        return 1;
    }

    auto channels_data = wav.channels();
    std::vector<std::vector<int32_t>> pcm(wav.num_channels);
    for (int c = 0; c < wav.num_channels; ++c) {
        pcm[c].resize(wav.num_frames);
        for (uint32_t i = 0; i < wav.num_frames; ++i)
            pcm[c][i] = float_to_pcm(channels_data[c][i]);
    }

    if (auto_spatial && wav.num_channels == 2) {
        std::cout << "Analyzing audio for spatial parameters...\n";

        slac::dsp::AudioFeatures features = slac::dsp::analyze_audio(
            pcm[0], pcm[1], 16, wav.sample_rate);
        slac::dsp::derive_spatial_params(features);

        wideness   = features.suggested_wideness_permille / 10;
        reverb_wet = features.suggested_reverb_wet_pct;
        preset_id  = features.suggested_preset_id;

        std::cout << "  Analysis:\n";
        std::cout << "    stereo_width:       " << features.stereo_width       << "\n";
        std::cout << "    stereo_correlation: " << features.stereo_correlation << "\n";
        std::cout << "    spectral_centroid:  " << features.spectral_centroid  << " Hz\n";
        std::cout << "    spectral_flux:      " << features.spectral_flux      << "\n";
        std::cout << "    rms_energy:         " << features.rms_energy         << "\n";
        std::cout << "  Derived:\n";
        std::cout << "    wideness:           " << wideness   << "%\n";
        std::cout << "    reverb_wet:         " << reverb_wet << "%\n";
        std::cout << "    preset_id:          " << preset_id  << "\n";
    } else if (auto_spatial) {
        std::cerr << "Warning: --auto-spatial requires stereo, ignoring.\n";
        auto_spatial = false;
    }

    if (!auto_spatial) {
        wideness   = get_opt_int(argc, argv, "--wideness",   100);
        reverb_wet = get_opt_int(argc, argv, "--reverb-wet",   0);
        preset_id  = get_opt_int(argc, argv, "--preset",       0);
    }

    wideness   = std::max(0, std::min(wideness,   150));
    reverb_wet = std::max(0, std::min(reverb_wet, 100));
    preset_id  = std::max(0, std::min(preset_id,  255));

    slac::SlacFormat fmt;
    fmt.sampleRate    = wav.sample_rate;
    fmt.bitsPerSample = 16;
    fmt.channelCount  = static_cast<uint8_t>(wav.num_channels);
    fmt.frameSize     = static_cast<uint32_t>(frame_size);
    fmt.mode          = (wav.num_channels == 2)
                          ? (adaptive ? slac::DecorrelationMode::Adaptive
                                      : slac::DecorrelationMode::MidSideFixed)
                          : slac::DecorrelationMode::None;

    bool has_spatial = (wideness != 100 || reverb_wet > 0 || preset_id > 0);

    slac::SpatMetadata spat;
    if (has_spatial) {
        spat.wideness_permille = static_cast<uint16_t>(wideness * 10);
        spat.reverb_wet_pct    = static_cast<uint8_t>(reverb_wet);
        spat.preset_id         = static_cast<uint8_t>(preset_id);
        spat.flags             = 0x02;
        fill_fallback(spat, spat.preset_id);
    }

    std::vector<uint8_t> slac_data;
    try {
        slac_data = slac::encodeSlacFile(
            pcm, fmt,
            has_spatial ? &spat : nullptr,
            lpc_order, lpc_shift,
            entropy_range);
    } catch (const std::exception& e) {
        std::cerr << "Encode error: " << e.what() << "\n";
        return 1;
    }

    if (!write_file(output_path, slac_data)) {
        std::cerr << "Error: cannot write: " << output_path << "\n";
        return 1;
    }

    uint64_t raw_size = static_cast<uint64_t>(wav.num_frames)
                      * wav.num_channels * 2;
    double ratio = (slac_data.size() > 0)
        ? static_cast<double>(raw_size) / static_cast<double>(slac_data.size())
        : 0.0;
    uint32_t num_frames = static_cast<uint32_t>(
        (wav.num_frames + frame_size - 1) / frame_size);

    std::cout << "Encoded: " << input_path << " -> " << output_path << "\n";
    std::cout << "  Channels:    " << static_cast<int>(wav.num_channels) << "\n";
    std::cout << "  Sample rate: " << wav.sample_rate << " Hz\n";
    std::cout << "  Samples:     " << wav.num_frames << "\n";
    std::cout << "  Frame size:  " << frame_size << " samples/frame\n";
    std::cout << "  Num frames:  " << num_frames << "\n";
    std::cout << "  Preset:      " << preset_name
              << " (adaptive=" << (adaptive ? "yes" : "no")
              << ", lpc=" << lpc_order << ")\n";
    std::cout << "  Entropy:     "
              << (entropy_range ? "range (EXPERIMENTAL)" : "rice") << "\n";
    std::cout << "  Input size:  " << raw_size << " bytes\n";
    std::cout << "  Output size: " << slac_data.size() << " bytes\n";
    std::cout << "  Ratio:       " << ratio << ":1\n";

    if (has_spatial) {
        std::cout << "  Spatial:     wideness=" << wideness
                  << "% wet=" << reverb_wet
                  << "% preset=" << preset_id << "\n";
    } else {
        std::cout << "  Spatial:     none (SLAC Core)\n";
    }

    return 0;
}

// ──────────────────────────────────────────────────────────────
// DECODE
// ──────────────────────────────────────────────────────────────

static int cmd_decode(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: slacodec-cli decode <input.slac> <output.wav> [options]\n";
        return 1;
    }

    using LM = slac::dsp::SpatialChainConfig::LimitMode;

    std::string input_path  = argv[2];
    std::string output_path = argv[3];
    bool spatial         = has_flag(argc, argv, "--spatial");
    bool adaptive_reverb = !has_flag(argc, argv, "--fixed-reverb");
    std::string ir_path   = get_opt(argc, argv, "--ir", "");
    std::string ir_dir    = get_opt(argc, argv, "--ir-dir", "./ir");
    std::string hrir_path = get_opt(argc, argv, "--hrir", "");

    std::vector<uint8_t> slac_data;
    if (!read_file(input_path, slac_data)) {
        std::cerr << "Error: cannot open: " << input_path << "\n";
        return 1;
    }

    slac::SlacFormat fmt;
    slac::SpatMetadata spat;
    slac::SeekTable seek;
    slac::HashInfo hash;
    std::vector<std::vector<int32_t>> pcm;

    try {
        pcm = slac::decodeSlacFile(slac_data, &fmt, &spat, &seek, &hash);
    } catch (const std::exception& e) {
        std::cerr << "Decode error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Decoded: " << input_path << "\n";
    std::cout << "  Channels:    " << static_cast<int>(fmt.channelCount) << "\n";
    std::cout << "  Sample rate: " << fmt.sampleRate << " Hz\n";
    std::cout << "  Samples:     " << (pcm.empty() ? 0 : pcm[0].size()) << "\n";
    std::cout << "  Frame size:  " << fmt.frameSize << "\n";
    std::cout << "  Num frames:  " << seek.frame_count << "\n";
    std::cout << "  Decorrelation: "
              << (fmt.mode == slac::DecorrelationMode::Adaptive ? "adaptive"
                : fmt.mode == slac::DecorrelationMode::MidSideFixed ? "fixed M/S"
                : "none") << "\n";

    if (hash.present && !hash.match) {
        std::cerr << "  WARNING: SHA-256 mismatch on decoded PCM!\n";
    }

    if (spatial && fmt.channelCount == 2) {
        std::cout << "  Applying spatial DSP (float chain + headroom)...\n";

        slac::dsp::SpatialChainConfig cfg;
        cfg.wideness  = spat.wideness_permille / 1000.0f;
        cfg.mono_safe = (spat.flags & 0x02u) != 0u;
	cfg.hrir_echo_trim = static_cast<float>(
            get_opt_int(argc, argv, "--hrir-echo-trim", 100)) / 100.0f;

        if (has_flag(argc, argv, "--no-limit")) {
            cfg.limit_mode = LM::None;
        } else {
            std::string lm = get_opt(argc, argv, "--limit-mode", "normalize");
            if (lm == "limit")         cfg.limit_mode = LM::Limit;
            else if (lm == "loudness") cfg.limit_mode = LM::Loudness;
            else if (lm == "none")     cfg.limit_mode = LM::None;
            else                       cfg.limit_mode = LM::Normalize;
        }

        slac::dsp::TrueStereoIR hrir;
        if (!hrir_path.empty()) {
            try {
                slac::dsp::WavData hw = slac::dsp::load_wav(hrir_path);
                auto chs = hw.channels();
                hrir.sample_rate = hw.sample_rate;
                if (chs.size() >= 4) {
                    hrir.ll = chs[0]; hrir.lr = chs[1];
                    hrir.rl = chs[2]; hrir.rr = chs[3];
                } else if (chs.size() == 2) {
                    hrir.ll = chs[0]; hrir.rr = chs[1];
                    hrir.lr.assign(chs[0].size(), 0.0f);
                    hrir.rl.assign(chs[1].size(), 0.0f);
                } else {
                    throw std::runtime_error("HRIR needs 2 or 4 channels");
                }
                cfg.hrir = &hrir;
                std::cout << "    HRIR:     " << hrir_path
                          << " (true-stereo 4-path, unity-gain)\n";
            } catch (const std::exception& e) {
                std::cerr << "    Warning: HRIR load failed (" << e.what() << ")\n";
            }
        }

        if (spat.preset_id > 0 && spat.reverb_wet_pct > 0) {
            cfg.reverb_enabled  = true;
            cfg.reverb_adaptive = adaptive_reverb;
            cfg.reverb = slac::dsp::reverb_params_from_descriptors(
                spat.fallback_category, spat.fallback_size, spat.fallback_decay,
                static_cast<float>(spat.reverb_wet_pct));

            slac::dsp::TrueStereoIR rir;
            std::string rpath = ir_path;
            if (rpath.empty())
                rpath = ir_dir + "/reverb_preset_"
                      + std::to_string(spat.preset_id) + ".wav";
            try {
                slac::dsp::WavData rw = slac::dsp::load_wav(rpath);
                auto chs = rw.channels();
                rir.sample_rate = rw.sample_rate;
                if (chs.size() >= 4) {
                    rir.ll = chs[0]; rir.lr = chs[1];
                    rir.rl = chs[2]; rir.rr = chs[3];
                } else if (chs.size() == 2) {
                    rir.ll = chs[0]; rir.rr = chs[1];
                    rir.lr.assign(chs[0].size(), 0.0f);
                    rir.rl.assign(chs[1].size(), 0.0f);
                }
                cfg.reverb_ir = &rir;
                std::cout << "    Reverb:   preset="
                          << static_cast<int>(spat.preset_id)
                          << " wet=" << static_cast<int>(spat.reverb_wet_pct)
                          << "% engine=IR convolution\n";
            } catch (...) {
                std::cout << "    Reverb:   preset="
                          << static_cast<int>(spat.preset_id)
                          << " wet=" << static_cast<int>(spat.reverb_wet_pct)
                          << "% engine="
                          << (adaptive_reverb ? "FDN synth (adaptive)"
                                              : "FDN synth (fixed)")
                          << "\n";
            }
        } else {
            std::cout << "    Reverb:   skipped (preset=0 or wet=0)\n";
        }

        slac::dsp::SpatialChainStats st =
            slac::dsp::apply_spatial_chain(pcm[0], pcm[1], fmt.bitsPerSample,
                                           fmt.sampleRate, cfg);

        std::cout << "    Wideness: " << (spat.wideness_permille / 10.0) << "%\n";
        std::cout << "    Peak in:  " << st.peak_input
                  << " (" << st.peak_input_db << " dBFS)\n";
        std::cout << "    Peak pre-limit: " << st.peak_before_limit
                  << " (" << st.peak_before_limit_db << " dBFS)\n";

        if (cfg.limit_mode == LM::None) {
            std::cout << "    Headroom: OFF (--no-limit / --limit-mode none)\n";
        } else if (cfg.limit_mode == LM::Normalize) {
            std::cout << "    Headroom: uniform " << st.uniform_gain_db
                      << " dB (peak align a fonte, zero pumping)\n";
        } else if (cfg.limit_mode == LM::Loudness) {
            std::cout << "    Headroom: loudness align " << st.uniform_gain_db
                      << " dB uniform + limiter max "
                      << st.max_gain_reduction_db << " dB\n";
        } else {
            std::cout << "    Limiter:  max " << st.max_gain_reduction_db
                      << " dB reduction\n";
        }
    } else if (spatial) {
        std::cout << "  Spatial DSP skipped (mono or no spat chunk).\n";
    } else {
        std::cout << "  Mode: SLAC Core (no spatial DSP).\n";
    }

    try {
        slac::write_wav(output_path, pcm, fmt.sampleRate);
    } catch (const std::exception& e) {
        std::cerr << "Error writing output: " << e.what() << "\n";
        return 1;
    }

    std::cout << "  Output: " << output_path << "\n";
    return 0;
}

// ──────────────────────────────────────────────────────────────
// VERIFY (bit-exact + SHA-256)
// ──────────────────────────────────────────────────────────────

static int cmd_verify(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: slacodec-cli verify <original.wav> <coded.slac>\n";
        return 1;
    }

    std::string wav_path  = argv[2];
    std::string slac_path = argv[3];

    slac::dsp::WavData wav;
    try {
        wav = slac::dsp::load_wav(wav_path);
    } catch (const std::exception& e) {
        std::cerr << "Error reading WAV: " << e.what() << "\n";
        return 1;
    }

    std::vector<uint8_t> slac_data;
    if (!read_file(slac_path, slac_data)) {
        std::cerr << "Error reading SLAC: " << slac_path << "\n";
        return 1;
    }

    std::vector<std::vector<int32_t>> decoded;
    slac::HashInfo hash;
    try {
        decoded = slac::decodeSlacFile(slac_data, nullptr, nullptr, nullptr, &hash);
    } catch (const std::exception& e) {
        std::cerr << "Decode error: " << e.what() << "\n";
        return 1;
    }

    if (decoded.size() != static_cast<size_t>(wav.num_channels)) {
        std::cerr << "VERIFY FAIL: channel count mismatch ("
                  << decoded.size() << " vs "
                  << static_cast<int>(wav.num_channels) << ")\n";
        return 1;
    }

    auto channels = wav.channels();
    uint64_t mismatches = 0;
    int64_t  first_index = -1;
    int      first_channel = -1;

    for (size_t c = 0; c < decoded.size(); ++c) {
        if (channels[c].size() != decoded[c].size()) {
            std::cout << "VERIFY FAIL: length mismatch ch " << c << ": "
                      << channels[c].size() << " vs " << decoded[c].size() << "\n";
            return 1;
        }
        size_t n = channels[c].size();
        for (size_t i = 0; i < n; ++i) {
            int32_t orig = float_to_pcm(channels[c][i]);
            if (orig != decoded[c][i]) {
                ++mismatches;
                if (first_index < 0) {
                    first_index = static_cast<int64_t>(i);
                    first_channel = static_cast<int>(c);
                }
            }
        }
    }

    if (mismatches != 0) {
        std::cout << "VERIFY FAIL: " << mismatches
                  << " mismatched samples; first at ch " << first_channel
                  << " index " << first_index
                  << " (t=" << (static_cast<double>(first_index) / wav.sample_rate)
                  << "s)\n";
        return 1;
    }

    std::cout << "VERIFY OK: bit-exact (" << wav.num_frames
              << " frames x " << static_cast<int>(wav.num_channels) << " ch)\n";

    if (hash.present) {
        std::cout << "  hash: " << (hash.match ? "SHA-256 OK (" : "MISMATCH (")
                  << slac::sha256_hex(hash.digest) << ")\n";
        if (!hash.match) return 1;
    } else {
        std::cout << "  hash: absent (arquivo anterior ao chunk hash)\n";
    }

    return 0;
}

// ──────────────────────────────────────────────────────────────
// GEN (corpus sintetico deterministico para golden files)
// ──────────────────────────────────────────────────────────────

static int cmd_gen(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: slacodec-cli gen <name> <out.wav>\n";
        return 1;
    }

    std::string name = argv[2];
    std::string out_path = argv[3];

    uint32_t sr = 48000;
    size_t n = static_cast<size_t>(sr) * 2;
    int ch = (name == "mono") ? 1 : 2;

    std::vector<std::vector<int32_t>> pcm(ch, std::vector<int32_t>(n, 0));

    uint32_t seed = 12345;
    auto rnd = [&]() -> int32_t {
        seed = seed * 1103515245u + 12345u;
        return static_cast<int32_t>(seed >> 16) - 16384;
    };

    const double pi = 3.14159265358979323846;

    if (name == "silence") {
        // permanece zerado
    } else if (name == "sine" || name == "mono") {
        for (size_t i = 0; i < n; ++i) {
            int32_t v = static_cast<int32_t>(12000.0 * std::sin(2.0 * pi * 440.0 * i / sr));
            for (int c = 0; c < ch; ++c) pcm[c][i] = v;
        }
    } else if (name == "noise") {
        for (int c = 0; c < ch; ++c)
            for (size_t i = 0; i < n; ++i) pcm[c][i] = rnd();
    } else if (name == "transient") {
        for (int c = 0; c < ch; ++c)
            for (size_t i = 0; i < n; ++i)
                pcm[c][i] = static_cast<int32_t>(3000.0 * std::sin(2.0 * pi * 200.0 * i / sr));
        for (size_t i = 0; i < n; i += sr / 4)
            for (int c = 0; c < ch; ++c)
                for (size_t j = i; j < i + 64 && j < n; ++j)
                    pcm[c][j] = (j == i) ? 30000
                        : static_cast<int32_t>(30000.0 * (1.0 - double(j - i) / 64.0));
    } else if (name == "wasted") {
        for (int c = 0; c < ch; ++c)
            for (size_t i = 0; i < n; ++i)
                pcm[c][i] = static_cast<int32_t>(1500.0 * std::sin(2.0 * pi * 330.0 * i / sr)) << 4;
    } else if (name == "stereo-wide") {
        for (size_t i = 0; i < n; ++i) {
            pcm[0][i] = static_cast<int32_t>(10000.0 * std::sin(2.0 * pi * 440.0 * i / sr)) + rnd() / 4;
            pcm[1][i] = static_cast<int32_t>(10000.0 * std::sin(2.0 * pi * 660.0 * i / sr)) + rnd() / 4;
        }
    } else if (name == "stereo-asym") {
        for (size_t i = 0; i < n; ++i) {
            pcm[0][i] = static_cast<int32_t>(20000.0 * std::sin(2.0 * pi * 440.0 * i / sr));
            pcm[1][i] = static_cast<int32_t>(800.0  * std::sin(2.0 * pi * 445.0 * i / sr));
        }
    } else {
        std::cerr << "Unknown gen name: " << name << "\n";
        return 1;
    }

    try {
        slac::write_wav(out_path, pcm, sr);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Generated " << out_path << " (" << name << ", " << n
              << " frames x " << ch << " ch)\n";
    return 0;
}

// ──────────────────────────────────────────────────────────────
// INFO
// ──────────────────────────────────────────────────────────────

static const char* category_name(uint8_t c) {
    switch (c) {
        case 0: return "None";
        case 1: return "Room";
        case 2: return "Chamber";
        case 3: return "Hall";
        case 4: return "Plate";
        case 5: return "Spring";
        case 6: return "Ambience";
        default: return "Unknown";
    }
}

static const char* decorrelation_name(slac::DecorrelationMode m) {
    switch (m) {
        case slac::DecorrelationMode::None:         return "None";
        case slac::DecorrelationMode::MidSideFixed: return "Mid/Side (fixed)";
        case slac::DecorrelationMode::Adaptive:     return "Adaptive (L/R, M/S, L/S, R/S)";
        default: return "Unknown";
    }
}

static int cmd_info(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: slacodec-cli info <input.slac>\n";
        return 1;
    }

    std::string input_path = argv[2];

    std::vector<uint8_t> slac_data;
    if (!read_file(input_path, slac_data)) {
        std::cerr << "Error: cannot open: " << input_path << "\n";
        return 1;
    }

    slac::SlacFormat fmt;
    slac::SpatMetadata spat;
    slac::SeekTable seek;
    slac::HashInfo hash;

    try {
        slac::decodeSlacFile(slac_data, &fmt, &spat, &seek, &hash);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "=== SLACodec File Info ===\n\n";

    std::cout << "[fmt]\n";
    std::cout << "  sample_rate:       " << fmt.sampleRate << "\n";
    std::cout << "  bits_per_sample:   " << static_cast<int>(fmt.bitsPerSample) << "\n";
    std::cout << "  channels:          " << static_cast<int>(fmt.channelCount) << "\n";
    std::cout << "  total_samples:     " << fmt.totalSamples << "\n";
    std::cout << "  frame_size:        " << fmt.frameSize << " samples/frame\n";
    std::cout << "  decorrelation:     " << decorrelation_name(fmt.mode) << "\n";
    std::cout << "  encoder_version:   "
              << static_cast<int>(fmt.encoderMajor) << "."
              << static_cast<int>(fmt.encoderMinor) << "\n";

    double duration = (fmt.sampleRate > 0)
        ? static_cast<double>(fmt.totalSamples) / static_cast<double>(fmt.sampleRate)
        : 0.0;

    int mins = static_cast<int>(duration) / 60;
    double secs = duration - mins * 60.0;
    std::cout << "  duration:          " << mins << "m " << secs << "s\n";

    std::cout << "\n[seek]\n";
    std::cout << "  frame_count:       " << seek.frame_count << "\n";
    std::cout << "  frame_size:        " << seek.frame_size << "\n";

    std::cout << "\n[spat]\n";
    std::cout << "  wideness:          " << (spat.wideness_permille / 10.0) << "%\n";
    std::cout << "  reverb_wet:        " << static_cast<int>(spat.reverb_wet_pct) << "%\n";
    std::cout << "  preset_id:         " << static_cast<int>(spat.preset_id) << "\n";
    std::cout << "  mono_safe:         " << ((spat.flags & 0x02) ? "yes" : "no") << "\n";
    std::cout << "  has_automation:    " << ((spat.flags & 0x01) ? "yes" : "no") << "\n";
    std::cout << "  fallback_category: " << category_name(spat.fallback_category) << "\n";
    std::cout << "  fallback_size:     " << static_cast<int>(spat.fallback_size) << "\n";
    std::cout << "  fallback_decay:    " << static_cast<int>(spat.fallback_decay) << "\n";

    std::cout << "\n[file]\n";
    std::cout << "  size:              " << slac_data.size() << " bytes\n";

    if (hash.present) {
        std::cout << "  pcm_sha256:        " << slac::sha256_hex(hash.digest)
                  << (hash.match ? " (OK)" : " (MISMATCH)") << "\n";
    }

    uint64_t raw_size = static_cast<uint64_t>(fmt.totalSamples)
                      * static_cast<uint64_t>(fmt.channelCount)
                      * static_cast<uint64_t>(fmt.bitsPerSample / 8);

    if (raw_size > 0 && slac_data.size() > 0) {
        double ratio = static_cast<double>(raw_size)
                     / static_cast<double>(slac_data.size());
        double savings = 100.0 * (1.0 - static_cast<double>(slac_data.size())
                                       / static_cast<double>(raw_size));
        std::cout << "  raw_pcm_size:      " << raw_size << " bytes\n";
        std::cout << "  compression:       " << ratio << ":1\n";
        std::cout << "  space_saved:       " << savings << "%\n";
    }

    return 0;
}

// ──────────────────────────────────────────────────────────────
// MAIN
// ──────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "--help" || command == "-h") {
        print_usage();
        return 0;
    }

    if (command == "--version" || command == "-v") {
        print_version();
        return 0;
    }

    try {
        if (command == "encode") return cmd_encode(argc, argv);
        if (command == "decode") return cmd_decode(argc, argv);
        if (command == "info")   return cmd_info(argc, argv);
        if (command == "verify") return cmd_verify(argc, argv);
        if (command == "gen")    return cmd_gen(argc, argv);

        std::cerr << "Unknown command: " << command << "\n\n";
        print_usage();
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
