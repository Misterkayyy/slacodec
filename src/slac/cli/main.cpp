// src/slac/cli/main.cpp
//
// SLACodec CLI — Spatial Lossless Audio Codec
// Subcommands: encode, decode, info, verify, gen, play
// Presets: fast | balanced | high | extreme

#include <slac/core/container.hpp>
#include <slac/core/wav_writer.hpp>
#include <slac/dsp/processor.hpp>
#include <slac/dsp/reverb_engine.hpp>
#include <slac/dsp/wav_loader.hpp>
#include "slac/dsp/audio_converter.hpp"
#include <slac/dsp/analyzer.hpp>
#include <slac/dsp/spatial_chain.hpp>
#include "slac/rt/realtime_player.hpp"
#include "slac/rt/audio_engine.hpp"
#include "slac/dsp/fdn_reverb.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static constexpr const char* SLAC_VERSION = "0.7.0";

// ═══════════════════════════════════════════════════════════════
// UI HELPERS (ProgressBar + Spinner)
// ═══════════════════════════════════════════════════════════════

class ProgressBar {
public:
    ProgressBar(size_t total, const std::string& label)
        : total_(total), label_(label), current_(0) {}

    void update(size_t current) {
        current_ = current;
        float percent = static_cast<float>(current_) / static_cast<float>(total_);
        int pos = static_cast<int>(40.0f * percent);

        std::cout << "\r" << label_ << " [";
        for (int i = 0; i < 40; ++i) {
            if (i < pos) std::cout << "=";
            else if (i == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(0) << (percent * 100.0f) << "%";
        std::cout.flush();
    }

    void done() {
        update(total_);
        std::cout << "\n";
    }

private:
    size_t total_, current_;
    std::string label_;
};

class Spinner {
public:
    Spinner(const std::string& label) : label_(label), active_(true) {
        thread_ = std::thread(&Spinner::run, this);
    }
    ~Spinner() {
        active_ = false;
        if (thread_.joinable()) thread_.join();
        std::cout << "\r" << std::string(80, ' ') << "\r";
    }

private:
    void run() {
        const char* frames[] = {"-", "\\", "|", "/"};
        int i = 0;
        while (active_) {
            std::cout << "\r" << frames[i] << " " << label_ << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            i = (i + 1) % 4;
        }
    }
    std::string label_;
    std::atomic<bool> active_;
    std::thread thread_;
};

// ═══════════════════════════════════════════════════════════════
// ARGUMENT HELPERS
// ═══════════════════════════════════════════════════════════════

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
    try { return std::stoi(s); } catch (...) { return default_val; }
}

// ═══════════════════════════════════════════════════════════════
// FILE I/O HELPERS
// ═══════════════════════════════════════════════════════════════

static bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;
    size_t size = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    out.resize(size);

    ProgressBar bar(size, "Reading");
    size_t chunk = 65536;
    size_t read_total = 0;
    while (read_total < size) {
        size_t to_read = std::min(chunk, size - read_total);
        in.read(reinterpret_cast<char*>(out.data() + read_total), to_read);
        read_total += to_read;
        bar.update(read_total);
    }
    bar.done();
    return true;
}

static bool write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    ProgressBar bar(data.size(), "Writing");
    size_t chunk = 65536;
    size_t written = 0;
    while (written < data.size()) {
        size_t to_write = std::min(chunk, data.size() - written);
        out.write(reinterpret_cast<const char*>(data.data() + written), to_write);
        written += to_write;
        bar.update(written);
    }
    bar.done();
    return true;
}

static int32_t float_to_pcm(float v, int bits) {
    float scale = static_cast<float>(1 << (bits - 1));
    float s = v * scale;
    float max_val = scale - 1.0f;
    float min_val = -scale;
    if (s > max_val) s = max_val;
    if (s < min_val) s = min_val;
    return static_cast<int32_t>(s);
}

// ═══════════════════════════════════════════════════════════════
// ENCODER PRESETS
// ═══════════════════════════════════════════════════════════════

struct EncoderPreset {
    const char* name;
    uint32_t frame_size;
    int max_lpc_order;
    int lpc_shift;
    bool adaptive;
    bool use_range_coding;
};

static EncoderPreset get_preset(const std::string& name) {
    if (name == "fast")     return {"fast",     2048,  4, 15, false, false};
    if (name == "balanced") return {"balanced", 4096,  8, 15, true,  false};
    if (name == "high")     return {"high",     4096, 12, 15, true,  false};
    if (name == "extreme")  return {"extreme",  4096, 16, 15, true,  false};
    return {"balanced", 4096, 8, 15, true, false};
}

// ═══════════════════════════════════════════════════════════════
// SPAT FALLBACK DESCRIPTORS
// ═══════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════
// ENCODE
// ═══════════════════════════════════════════════════════════════

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

    int frame_size = get_opt_int(argc, argv, "--frame-size", static_cast<int>(preset.frame_size));
    int lpc_order  = get_opt_int(argc, argv, "--lpc-order", preset.max_lpc_order);
    int lpc_shift  = get_opt_int(argc, argv, "--lpc-shift", preset.lpc_shift);

    bool force_adaptive = has_flag(argc, argv, "--adaptive");
    bool force_fixed    = has_flag(argc, argv, "--fixed-ms");
    bool adaptive = force_adaptive || (!force_fixed && preset.adaptive);
    if (force_fixed) adaptive = false;

    bool entropy_range = has_flag(argc, argv, "--entropy-range") || preset.use_range_coding;

    // ── Load input ──
    slac::dsp::WavData wav;
    try {
        int src_bits = 0;
        wav = slac::dsp::load_any_audio(input_path, &src_bits);
        if (src_bits > 0) {
            std::cout << "  (converted from " << src_bits << "-bit source via ffmpeg)\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading input: " << e.what() << "\n";
        return 1;
    }

    if (wav.num_channels < 1 || wav.num_channels > 2) {
        std::cerr << "Error: only mono and stereo supported (got "
                  << static_cast<int>(wav.num_channels) << " ch)\n";
        return 1;
    }

    // ── Bit-depth handling ──
    int effective_bits = wav.bits_per_sample;
    if (wav.bits_per_sample > 24) {
        std::cerr << "Warning: input is " << wav.bits_per_sample
                  << "-bit. Converting to 24-bit.\n";
        effective_bits = 24;
    } else if (wav.bits_per_sample == 8) {
        std::cerr << "Warning: input is 8-bit. Converting to 16-bit.\n";
        effective_bits = 16;
    }

    // ── Float -> PCM ──
    auto channels_data = wav.channels();
    std::vector<std::vector<int32_t>> pcm(wav.num_channels);
    {
        ProgressBar bar(wav.num_frames, "Converting to PCM");
        for (int c = 0; c < wav.num_channels; ++c) {
            pcm[c].resize(wav.num_frames);
            for (uint32_t i = 0; i < wav.num_frames; ++i) {
                pcm[c][i] = float_to_pcm(channels_data[c][i], effective_bits);
            }
            if (c == 0) bar.update(wav.num_frames);
        }
        bar.done();
    }

    // ── Spatial analysis ──
    std::vector<slac::core::AutoKeyframe> auto_kfs;
    int wideness   = 100;
    int reverb_wet = 0;
    int preset_id  = 0;

    if (auto_spatial && wav.num_channels == 2) {
        std::cout << "Analyzing audio for spatial parameters...\n";

        slac::dsp::AudioFeatures features = slac::dsp::analyze_audio(
            pcm[0], pcm[1], effective_bits, wav.sample_rate);
        slac::dsp::derive_spatial_params(features);

        wideness   = features.suggested_wideness_permille / 10;
        reverb_wet = features.suggested_reverb_wet_pct;
        preset_id  = features.suggested_preset_id;

        auto_kfs = slac::dsp::generate_auto_keyframes(pcm, effective_bits, wav.sample_rate);
        std::cout << "  Generated " << auto_kfs.size() << " spatial keyframes.\n";
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

    // ── Build format ──
    slac::SlacFormat fmt;
    fmt.sampleRate    = wav.sample_rate;
    fmt.bitsPerSample = static_cast<uint8_t>(effective_bits);
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

    if (!auto_kfs.empty() && has_spatial) {
        spat.flags |= 0x01;
    }

    const std::vector<slac::core::AutoKeyframe>* auto_kfs_ptr = nullptr;
    if (!auto_kfs.empty()) {
        auto_kfs_ptr = &auto_kfs;
    }

    // ── Encode ──
    std::vector<uint8_t> slac_data;
    {
        Spinner spin("Encoding");
        try {
            slac_data = slac::encodeSlacFile(
                pcm, fmt,
                has_spatial ? &spat : nullptr,
                lpc_order, lpc_shift,
                entropy_range,
                auto_kfs_ptr);
        } catch (const std::exception& e) {
            std::cerr << "Encode error: " << e.what() << "\n";
            return 1;
        }
    }

    // ── Write output ──
    if (!write_file(output_path, slac_data)) {
        std::cerr << "Error: cannot write: " << output_path << "\n";
        return 1;
    }

    // ── Summary ──
    uint64_t raw_size = static_cast<uint64_t>(wav.num_frames)
                      * wav.num_channels * (effective_bits / 8);
    double ratio = (slac_data.size() > 0)
        ? static_cast<double>(raw_size) / static_cast<double>(slac_data.size())
        : 0.0;

    std::cout << "\n";
    std::cout << "  Input:       " << input_path << "\n";
    std::cout << "  Output:      " << output_path << "\n";
    std::cout << "  Format:      " << wav.sample_rate << " Hz / "
              << effective_bits << "-bit / "
              << static_cast<int>(wav.num_channels) << " ch\n";
    std::cout << "  Preset:      " << preset_name << "\n";
    std::cout << "  Raw size:    " << raw_size << " bytes\n";
    std::cout << "  SLAC size:   " << slac_data.size() << " bytes\n";
    std::cout << "  Ratio:       " << std::fixed << std::setprecision(2) << ratio << ":1\n";

    if (has_spatial) {
        std::cout << "  Spatial:     wideness=" << wideness
                  << "% wet=" << reverb_wet
                  << "% preset=" << preset_id << "\n";
    }

    return 0;
}

// ═══════════════════════════════════════════════════════════════
// DECODE
// ═══════════════════════════════════════════════════════════════

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
    bool no_auto         = has_flag(argc, argv, "--no-auto");
    bool partitioned_conv = has_flag(argc, argv, "--partitioned");

    std::vector<uint8_t> slac_data;
    if (!read_file(input_path, slac_data)) {
        std::cerr << "Error: cannot open: " << input_path << "\n";
        return 1;
    }

    slac::SlacFormat fmt;
    slac::SpatMetadata spat;
    slac::SeekTable seek;
    slac::HashInfo hash;
    std::vector<slac::core::AutoKeyframe> auto_kfs;
    std::vector<std::vector<int32_t>> pcm;

    {
        Spinner spin("Decoding");
        try {
            pcm = slac::decodeSlacFile(slac_data, &fmt, &spat, &seek, &hash, true, &auto_kfs);
        } catch (const std::exception& e) {
            std::cerr << "Decode error: " << e.what() << "\n";
            return 1;
        }
    }

    if (hash.present && !hash.match) {
        std::cerr << "Warning: SHA-256 mismatch on decoded PCM!\n";
    }

    if (spatial && fmt.channelCount == 2) {
        slac::dsp::SpatialChainConfig cfg;
        cfg.wideness  = spat.wideness_permille / 1000.0f;
        cfg.mono_safe = (spat.flags & 0x02u) != 0u;
        cfg.hrir_echo_trim = static_cast<float>(
            get_opt_int(argc, argv, "--hrir-echo-trim", 100)) / 100.0f;
        cfg.use_partitioned_conv = partitioned_conv;
        cfg.conv_block_size = 128;

        if (!auto_kfs.empty() && !no_auto) {
            cfg.auto_keyframes = auto_kfs;
            cfg.use_auto = true;
        }

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
            } catch (const std::exception& e) {
                std::cerr << "Warning: HRIR load failed (" << e.what() << ")\n";
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
            } catch (...) {}
        }

        {
            Spinner spin("Applying spatial DSP");
            slac::dsp::apply_spatial_chain(pcm[0], pcm[1], fmt.bitsPerSample,
                                           fmt.sampleRate, cfg);
        }
    }

    {
        Spinner spin("Writing WAV");
        try {
            slac::write_wav(output_path, pcm, fmt.sampleRate);
        } catch (const std::exception& e) {
            std::cerr << "Error writing output: " << e.what() << "\n";
            return 1;
        }
    }

    std::cout << "\n";
    std::cout << "  Input:       " << input_path << "\n";
    std::cout << "  Output:      " << output_path << "\n";
    std::cout << "  Format:      " << fmt.sampleRate << " Hz / "
              << static_cast<int>(fmt.bitsPerSample) << "-bit / "
              << static_cast<int>(fmt.channelCount) << " ch\n";
    std::cout << "  Samples:     " << (pcm.empty() ? 0 : pcm[0].size()) << "\n";

    return 0;
}

// ═══════════════════════════════════════════════════════════════
// PLAY
// ═══════════════════════════════════════════════════════════════

static int cmd_play(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: slacodec-cli play <input.slac> [options]\n";
        std::cerr << "Options:\n";
        std::cerr << "  --spatial              Apply spatial chain\n";
        std::cerr << "  --hrir <path>          True-stereo HRIR\n";
        std::cerr << "  --ir <path>            Convolution reverb IR\n";
        std::cerr << "  --partitioned          Use partitioned convolver\n";
        std::cerr << "  --makeup-db <N>        Volume boost post-chain (default 6 dB)\n";
        std::cerr << "  --no-auto              Disable automation keyframes\n";
        std::cerr << "  --limit-mode <mode>    limit|normalize|loudness|none\n";
        return 1;
    }

    using LM = slac::dsp::SpatialChainConfig::LimitMode;

    std::string input_path = argv[2];
    bool spatial          = has_flag(argc, argv, "--spatial");
    bool partitioned_conv = has_flag(argc, argv, "--partitioned");
    bool no_auto          = has_flag(argc, argv, "--no-auto");
    std::string hrir_path = get_opt(argc, argv, "--hrir", "");
    std::string ir_path   = get_opt(argc, argv, "--ir", "");

    slac::dsp::SpatialChainConfig cfg;

    slac::rt::RealtimePlayer player;
    if (!player.open(input_path, cfg, 128)) {
        std::cerr << "Error: cannot open: " << input_path << "\n";
        return 1;
    }

    std::cout << "Playing: " << input_path << "\n";
    std::cout << "  " << player.sample_rate() << " Hz / "
              << static_cast<int>(player.channels()) << " ch / "
              << player.total_samples() << " samples\n";

    if (spatial && player.channels() == 2) {
        slac::dsp::SpatialChainConfig cfg_spatial;
        const auto& spat = player.spat();
        const auto& auto_kfs = player.auto_keyframes();

        if (!auto_kfs.empty() && !no_auto) {
            cfg_spatial.auto_keyframes = auto_kfs;
            cfg_spatial.use_auto = true;
        }

        cfg_spatial.wideness = spat.wideness_permille / 1000.0f;
        cfg_spatial.mono_safe = (spat.flags & 0x02u) != 0u;
        cfg_spatial.hrir_echo_trim = static_cast<float>(
            get_opt_int(argc, argv, "--hrir-echo-trim", 100)) / 100.0f;
        cfg_spatial.use_partitioned_conv = partitioned_conv;
        cfg_spatial.conv_block_size = 128;
        cfg_spatial.makeup_gain_db = static_cast<float>(
            get_opt_int(argc, argv, "--makeup-db", 6));

        std::string lm = get_opt(argc, argv, "--limit-mode", "normalize");
        if (lm == "limit")         cfg_spatial.limit_mode = LM::Limit;
        else if (lm == "loudness") cfg_spatial.limit_mode = LM::Loudness;
        else if (lm == "none")     cfg_spatial.limit_mode = LM::None;
        else                       cfg_spatial.limit_mode = LM::Normalize;
        cfg_spatial.ceiling = 0.988f;

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
                }
                cfg_spatial.hrir = &hrir;
                cfg_spatial.hrir_unity_gain = true;
            } catch (const std::exception& e) {
                std::cerr << "Error loading HRIR: " << e.what() << "\n";
                return 1;
            }
        }

        if (spat.preset_id > 0 && spat.reverb_wet_pct > 0) {
            cfg_spatial.reverb_enabled = true;
            cfg_spatial.reverb_adaptive = true;
            cfg_spatial.reverb = slac::dsp::reverb_params_from_descriptors(
                spat.fallback_category, spat.fallback_size, spat.fallback_decay,
                static_cast<float>(spat.reverb_wet_pct));

            slac::dsp::TrueStereoIR rir;
            bool loaded_rir = false;
            if (!ir_path.empty()) {
                try {
                    slac::dsp::WavData rw = slac::dsp::load_wav(ir_path);
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
                    cfg_spatial.reverb_ir = &rir;
                    loaded_rir = true;
                } catch (...) {}
            }
        }

        player.reconfigure(cfg_spatial);
    }

    slac::rt::AudioEngine engine;
    if (!engine.start(player.sample_rate(), player.channels(), 128, &player)) {
        std::cerr << "Error: failed to start audio engine\n";
        return 1;
    }

    player.start();
    if (!engine.play()) {
        std::cerr << "Error: failed to start playback\n";
        player.stop();
        return 1;
    }

#if defined(__ANDROID__)
    std::cout << "  Backend: AAudio (low-latency)\n";
#elif defined(_WIN32)
    std::cout << "  Backend: WASAPI\n";
#endif
    std::cout << "  Press Ctrl+C to stop.\n";

    while (engine.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    engine.stop();
    player.stop();
    std::cout << "Playback finished.\n";
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// VERIFY
// ═══════════════════════════════════════════════════════════════

static int cmd_verify(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: slacodec-cli verify <original.wav> <coded.slac>\n";
        return 1;
    }

    std::string wav_path  = argv[2];
    std::string slac_path = argv[3];

    slac::dsp::WavData wav;
    try {
        int src_bits = 0;
        wav = slac::dsp::load_any_audio(wav_path, &src_bits);
        if (src_bits > 0) {
            std::cout << "  (converted from " << src_bits << "-bit source via ffmpeg)\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading input: " << e.what() << "\n";
        return 1;
    }

    std::vector<uint8_t> slac_data;
    if (!read_file(slac_path, slac_data)) {
        std::cerr << "Error reading SLAC: " << slac_path << "\n";
        return 1;
    }

    std::vector<std::vector<int32_t>> decoded;
    slac::HashInfo hash;
    slac::SlacFormat fmt;
    try {
        decoded = slac::decodeSlacFile(slac_data, &fmt, nullptr, nullptr, &hash);
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

    {
        ProgressBar bar(wav.num_frames, "Comparing samples");
        for (size_t c = 0; c < decoded.size(); ++c) {
            if (channels[c].size() != decoded[c].size()) {
                std::cout << "\nVERIFY FAIL: length mismatch ch " << c << ": "
                          << channels[c].size() << " vs " << decoded[c].size() << "\n";
                return 1;
            }
            size_t n = channels[c].size();
            for (size_t i = 0; i < n; ++i) {
                int32_t orig = float_to_pcm(channels[c][i], static_cast<int>(fmt.bitsPerSample));
                if (orig != decoded[c][i]) {
                    ++mismatches;
                    if (first_index < 0) {
                        first_index = static_cast<int64_t>(i);
                        first_channel = static_cast<int>(c);
                    }
                }
                if (c == 0 && i % 8192 == 0) bar.update(i);
            }
        }
        bar.done();
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
        std::cout << "  SHA-256: " << (hash.match ? "OK" : "MISMATCH")
                  << " (" << slac::sha256_hex(hash.digest) << ")\n";
        if (!hash.match) return 1;
    } else {
        std::cout << "  SHA-256: absent (file predates hash chunk)\n";
    }

    return 0;
}

// ═══════════════════════════════════════════════════════════════
// GEN
// ═══════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════
// INFO
// ═══════════════════════════════════════════════════════════════

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
        case slac::DecorrelationMode::Adaptive:     return "Adaptive";
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
    std::vector<slac::core::AutoKeyframe> auto_kfs;
    slac::SlacMetadata meta;
    slac::SlacCover covr;

    try {
        slac::decodeSlacFile(slac_data, &fmt, &spat, &seek, &hash, true, &auto_kfs, &meta, &covr);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    auto print_row = [](const std::string& key, const std::string& val) {
        std::cout << "  " << std::left << std::setw(20) << key << " " << val << "\n";
    };

    std::cout << "\n[Audio]\n";
    print_row("Sample rate", std::to_string(fmt.sampleRate) + " Hz");
    print_row("Bit depth", std::to_string(fmt.bitsPerSample) + "-bit");
    print_row("Channels", std::to_string(fmt.channelCount));
    print_row("Total samples", std::to_string(fmt.totalSamples));
    print_row("Frame size", std::to_string(fmt.frameSize) + " samples");
    print_row("Decorrelation", decorrelation_name(fmt.mode));

    double duration = (fmt.sampleRate > 0)
        ? static_cast<double>(fmt.totalSamples) / static_cast<double>(fmt.sampleRate)
        : 0.0;
    int mins = static_cast<int>(duration) / 60;
    double secs = duration - mins * 60.0;
    std::cout << "  " << std::left << std::setw(20) << "Duration"
              << mins << "m " << std::fixed << std::setprecision(1) << secs << "s\n";

    std::cout << "\n[Spatial]\n";
    print_row("Wideness", std::to_string(spat.wideness_permille / 10.0) + "%");
    print_row("Reverb wet", std::to_string(static_cast<int>(spat.reverb_wet_pct)) + "%");
    print_row("Preset ID", std::to_string(static_cast<int>(spat.preset_id)));
    print_row("Category", category_name(spat.fallback_category));
    print_row("Automation", (spat.flags & 0x01) ? "yes" : "no");
    if (!auto_kfs.empty()) {
        print_row("Keyframes", std::to_string(auto_kfs.size()));
    }

    std::cout << "\n[File]\n";
    print_row("Size", std::to_string(slac_data.size()) + " bytes");

    uint64_t raw_size = static_cast<uint64_t>(fmt.totalSamples)
                      * static_cast<uint64_t>(fmt.channelCount)
                      * static_cast<uint64_t>(fmt.bitsPerSample / 8);

    if (raw_size > 0 && slac_data.size() > 0) {
        double ratio = static_cast<double>(raw_size)
                     / static_cast<double>(slac_data.size());
        double savings = 100.0 * (1.0 - static_cast<double>(slac_data.size())
                                       / static_cast<double>(raw_size));
        print_row("Raw PCM size", std::to_string(raw_size) + " bytes");
        print_row("Compression", std::to_string(ratio).substr(0, 4) + ":1");
        print_row("Space saved", std::to_string(static_cast<int>(savings)) + "%");
    }

    if (hash.present) {
        print_row("SHA-256", slac::sha256_hex(hash.digest) + (hash.match ? " (OK)" : " (MISMATCH)"));
    }

    if (!meta.fields.empty()) {
        std::cout << "\n[Metadata]\n";
        for (const auto& [key, value] : meta.fields) {
            print_row(key, value);
        }
    }

    if (!covr.data.empty()) {
        std::cout << "\n[Cover]\n";
        print_row("MIME", covr.mime);
        print_row("Size", std::to_string(covr.data.size()) + " bytes");
    }

    return 0;
}

// ═══════════════════════════════════════════════════════════════
// USAGE / VERSION
// ═══════════════════════════════════════════════════════════════

static void print_usage() {
    std::cout << "SLACodec CLI v" << SLAC_VERSION << " - Spatial Lossless Audio Codec\n\n";
    std::cout << "Usage:\n";
    std::cout << "  slacodec-cli encode <input.wav> <output.slac> [options]\n";
    std::cout << "  slacodec-cli decode <input.slac> <output.wav> [options]\n";
    std::cout << "  slacodec-cli play   <input.slac> [options]\n";
    std::cout << "  slacodec-cli info   <input.slac>\n";
    std::cout << "  slacodec-cli verify <original.wav> <coded.slac>\n";
    std::cout << "  slacodec-cli gen    <name> <out.wav>\n\n";
    std::cout << "Encode options:\n";
    std::cout << "  --preset-encode <name>   fast|balanced|high|extreme\n";
    std::cout << "  --adaptive               Force adaptive stereo decorrelation\n";
    std::cout << "  --fixed-ms               Force fixed M/S\n";
    std::cout << "  --frame-size <n>         Samples per frame\n";
    std::cout << "  --lpc-order <n>          Max LPC order\n";
    std::cout << "  --entropy-range          Use experimental range coder\n";
    std::cout << "  --auto-spatial           Auto-derive spatial params from audio\n";
    std::cout << "  --wideness <0-150>       Stereo wideness %\n";
    std::cout << "  --reverb-wet <0-100>     Reverb wet %\n";
    std::cout << "  --preset <0-255>         Reverb preset ID\n\n";
    std::cout << "Decode options:\n";
    std::cout << "  --spatial                Apply spatial DSP chain\n";
    std::cout << "  --hrir <path>            True-stereo HRIR\n";
    std::cout << "  --ir <path>              Convolution reverb IR\n";
    std::cout << "  --no-auto                Disable spatial automation\n";
    std::cout << "  --limit-mode <m>         normalize|loudness|limit|none\n\n";
    std::cout << "Play options:\n";
    std::cout << "  --spatial                Apply spatial chain\n";
    std::cout << "  --hrir <path>            True-stereo HRIR\n";
    std::cout << "  --ir <path>              Convolution reverb IR\n";
    std::cout << "  --makeup-db <N>          Volume boost post-chain\n";
    std::cout << "  --no-auto                Disable automation keyframes\n\n";
    std::cout << "Gen names: silence | sine | noise | transient | wasted | mono | stereo-wide | stereo-asym\n";
}

static void print_version() {
    std::cout << "slacodec-cli " << SLAC_VERSION << "\n";
}

// ═══════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════

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
        if (command == "play")   return cmd_play(argc, argv);
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