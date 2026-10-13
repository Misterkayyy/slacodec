#pragma once

#include "wav_loader.hpp"

#include <string>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <random>
#include <stdexcept>
#include <cctype>

// popen/pclose: MinGW usa _popen/_pclose
#if defined(_WIN32)
#  define SLAC_POPEN       _popen
#  define SLAC_PCLOSE      _pclose
#  define SLAC_NULL_DEVICE "2>nul"
#else
#  define SLAC_POPEN       popen
#  define SLAC_PCLOSE      pclose
#  define SLAC_NULL_DEVICE "2>/dev/null"
#endif

namespace slac::dsp {

// ═══════════════════════════════════════════════════════════════════════════
// Precisa de conversão? (tudo que não for WAV)
// ═══════════════════════════════════════════════════════════════════════════
inline bool needs_conversion(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return !(ext == ".wav" || ext == ".wave");
}

// ═══════════════════════════════════════════════════════════════════════════
// Escapar caminho pro shell (aspas + escape interno)
// ═══════════════════════════════════════════════════════════════════════════
inline std::string shell_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// RAII guard: remove o temp automaticamente (mesmo em exception)
// ═══════════════════════════════════════════════════════════════════════════
class TempFileGuard {
public:
    explicit TempFileGuard(std::string path) : path_(std::move(path)) {}
    ~TempFileGuard() { cleanup(); }

    TempFileGuard(const TempFileGuard&) = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;
    TempFileGuard(TempFileGuard&& other) noexcept : path_(std::move(other.path_)) {
        other.path_.clear();
    }

    const std::string& path() const { return path_; }
    bool empty() const { return path_.empty(); }

    void cleanup() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
            path_.clear();
        }
    }

private:
    std::string path_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Nome de temp único (timestamp + random)
// ═══════════════════════════════════════════════════════════════════════════
inline std::string make_temp_wav_path() {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device rd;
    std::string name = "slac_tmp_" + std::to_string(now) + "_"
                     + std::to_string(rd()) + ".wav";
    return (std::filesystem::temp_directory_path() / name).string();
}

// ═══════════════════════════════════════════════════════════════════════════
// Detectar bit-depth original via ffprobe
// Retorna 16/24/32 ou 0 se desconhecido (MP3/Opus às vezes retornam 0)
// ═══════════════════════════════════════════════════════════════════════════
inline int detect_bit_depth(const std::string& path) {
    std::string cmd = "ffprobe -v error -select_streams a:0 "
                      "-show_entries stream=bits_per_raw_sample "
                      "-of default=nw=1:nk=1 "
                    + shell_escape(path) + " " SLAC_NULL_DEVICE;

    FILE* pipe = SLAC_POPEN(cmd.c_str(), "r");
    if (!pipe) return 0;

    char buffer[64] = {0};
    if (!fgets(buffer, sizeof(buffer), pipe)) {
        SLAC_PCLOSE(pipe);
        return 0;
    }
    SLAC_PCLOSE(pipe);

    int bits = std::atoi(buffer);
    return (bits > 0) ? bits : 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Converter qualquer formato -> WAV com o bit-depth escolhido
// ═══════════════════════════════════════════════════════════════════════════
inline std::string convert_to_wav(const std::string& input_path, int target_bits) {
    std::string codec = (target_bits >= 24) ? "pcm_s24le" : "pcm_s16le";
    std::string temp_path = make_temp_wav_path();

    // -y: overwrite | -vn: descarta capa embutida | -loglevel error: silencioso
    std::string cmd = "ffmpeg -y -loglevel error -i " + shell_escape(input_path)
                    + " -vn -c:a " + codec + " " + shell_escape(temp_path)
                    + " " SLAC_NULL_DEVICE;

    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        throw std::runtime_error(
            "ffmpeg failed (exit " + std::to_string(ret) + "). Is ffmpeg in PATH?");
    }
    if (!std::filesystem::exists(temp_path)) {
        throw std::runtime_error("ffmpeg produced no output");
    }
    return temp_path;
}

// ═══════════════════════════════════════════════════════════════════════════
// Função de alto nível: carrega QUALQUER formato como WavData
//   WAV        -> load direto
//   MP3/FLAC/  -> detecta bit-depth via ffprobe
//   OPUS/etc      converte pra WAV (16 ou 24-bit) e carrega
//
// out_src_bits (opcional): recebe o bit-depth detectado do arquivo original
// ═══════════════════════════════════════════════════════════════════════════
inline WavData load_any_audio(const std::string& path, int* out_src_bits = nullptr) {
    if (out_src_bits) *out_src_bits = 0;

    if (!needs_conversion(path)) {
        return load_wav(path);
    }

    int src_bits = detect_bit_depth(path);
    int target_bits = (src_bits >= 24) ? 24 : 16;

    std::string temp = convert_to_wav(path, target_bits);
    TempFileGuard guard(temp);  // remove o temp ao sair do escopo

    WavData wav = load_wav(temp);
    if (out_src_bits) *out_src_bits = src_bits;
    return wav;
}

} // namespace slac::dsp