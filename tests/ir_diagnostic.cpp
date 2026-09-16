#include <slac/dsp/wav_loader.hpp>

#include <cmath>
#include <iostream>
#include <string>

using namespace slac::dsp;

int main(int argc, char** argv) {
    std::string path = (argc > 1) ? argv[1] : "ir/surround_crossfeed.wav";

    WavData wav;
    try {
        wav = load_wav(path);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "=== IR Diagnostic ===\n\n";
    std::cout << "File:        " << path << "\n";
    std::cout << "Channels:    " << wav.num_channels << "\n";
    std::cout << "Sample rate: " << wav.sample_rate << " Hz\n";
    std::cout << "Frames:      " << wav.num_frames << "\n";
    std::cout << "Duration:    "
              << (static_cast<double>(wav.num_frames) / wav.sample_rate * 1000.0)
              << " ms\n\n";

    auto ch = wav.channels();

    const char* labels[] = {"L->L (ch0)", "L->R (ch1)", "R->L (ch2)", "R->R (ch3)"};

    for (int c = 0; c < wav.num_channels && c < 4; ++c) {
        float peak = 0.0f;
        double rms_sum = 0.0;

        for (auto s : ch[c]) {
            peak = std::max(peak, std::fabs(s));
            rms_sum += static_cast<double>(s) * s;
        }

        float rms = static_cast<float>(std::sqrt(rms_sum / ch[c].size()));

        std::cout << "Channel " << c << " [" << labels[c] << "]:\n";
        std::cout << "  Peak:  " << peak << "\n";
        std::cout << "  RMS:   " << rms << "\n";
        std::cout << "  dBFS:  " << (20.0f * std::log10(rms + 1e-10f)) << " dB\n\n";
    }

    // Cross-correlations for the X pattern.
    if (wav.num_channels >= 4) {
        std::cout << "=== True-Stereo X Pattern Check ===\n\n";

        auto cross_corr = [&](int a, int b) -> float {
            size_t n = std::min(ch[a].size(), ch[b].size());
            double num = 0.0, ea = 0.0, eb = 0.0;
            for (size_t i = 0; i < n; ++i) {
                num += static_cast<double>(ch[a][i]) * ch[b][i];
                ea  += static_cast<double>(ch[a][i]) * ch[a][i];
                eb  += static_cast<double>(ch[b][i]) * ch[b][i];
            }
            double den = std::sqrt(ea * eb);
            return (den > 1e-10) ? static_cast<float>(num / den) : 0.0f;
        };

        std::cout << "  L->L vs R->R (direct symmetry): " << cross_corr(0, 3) << "\n";
        std::cout << "  L->R vs R->L (crossfeed symmetry): " << cross_corr(1, 2) << "\n";
        std::cout << "  L->L vs L->R (direct vs crossfeed): " << cross_corr(0, 1) << "\n";
        std::cout << "  R->R vs R->L (direct vs crossfeed): " << cross_corr(3, 2) << "\n";

        // Peak positions for each channel.
        std::cout << "\nPeak positions:\n";
        for (int c = 0; c < 4; ++c) {
            size_t peak_pos = 0;
            float peak_val = 0.0f;
            for (size_t i = 0; i < ch[c].size(); ++i) {
                if (std::fabs(ch[c][i]) > peak_val) {
                    peak_val = std::fabs(ch[c][i]);
                    peak_pos = i;
                }
            }
            double ms = static_cast<double>(peak_pos) / wav.sample_rate * 1000.0;
            std::cout << "  " << labels[c] << ": sample " << peak_pos
                      << " (" << ms << " ms), peak=" << peak_val << "\n";
        }
    }

    return 0;
}
