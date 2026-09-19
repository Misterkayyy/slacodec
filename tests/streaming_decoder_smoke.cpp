#include "slac/rt/streaming_decoder.hpp"
#include "slac/core/container.hpp"

#include <fstream>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace slac;
using namespace slac::rt;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file.slac>\n";
        return 1;
    }

    int failures = 0;
    const char* path = argv[1];

    // ── Teste 1: abrir e ler metadata ───────────────────────
    StreamingDecoder dec;
    if (!StreamingDecoder::open(path, dec)) {
        std::cerr << "FAIL: could not open " << path << "\n";
        return 1;
    }

    std::cout << "[TEST] open ok\n";
    std::cout << "  sample_rate: " << dec.sample_rate() << "\n";
    std::cout << "  channels: " << static_cast<int>(dec.channels()) << "\n";
    std::cout << "  bits_per_sample: " << static_cast<int>(dec.bits_per_sample()) << "\n";
    std::cout << "  total_samples: " << dec.total_samples() << "\n";

    // ── Teste 2: leitura sequencial ─────────────────────────
    {
        std::vector<std::vector<int32_t>> pcm;
        size_t chunk_size = 4096;
        uint64_t total_read = 0;
        size_t reads = 0;

        while (true) {
            size_t n = dec.read(pcm, chunk_size);
            if (n == 0) break;
            total_read += n;
            ++reads;
        }

        bool ok = (total_read == dec.total_samples());
        std::cout << "[TEST] sequential read ok=" << (ok ? "yes" : "no")
                  << " total=" << total_read << " reads=" << reads << "\n";
        if (!ok) { std::cerr << "FAIL: sequential read\n"; ++failures; }
    }

    // ── Teste 3: seek para o início e reler ─────────────────
    {
        if (!dec.seek_to_sample(0)) {
            std::cerr << "FAIL: seek_to_sample(0)\n";
            ++failures;
        } else {
            std::vector<std::vector<int32_t>> pcm;
            size_t n = dec.read(pcm, 1000);
            bool ok = (n == 1000) && (dec.position() == 1000);
            std::cout << "[TEST] seek(0) + read ok=" << (ok ? "yes" : "no")
                      << " read=" << n << " pos=" << dec.position() << "\n";
            if (!ok) { std::cerr << "FAIL: seek(0) + read\n"; ++failures; }
        }
    }

    // ── Teste 4: seek para o meio e ler ─────────────────────
    {
        uint64_t mid = dec.total_samples() / 2;
        if (!dec.seek_to_sample(mid)) {
            std::cerr << "FAIL: seek_to_sample(mid)\n";
            ++failures;
        } else {
            std::vector<std::vector<int32_t>> pcm;
            size_t n = dec.read(pcm, 1000);
            bool ok = (n == 1000) && (dec.position() == mid + 1000);
            std::cout << "[TEST] seek(mid) + read ok=" << (ok ? "yes" : "no")
                      << " read=" << n << " pos=" << dec.position() << "\n";
            if (!ok) { std::cerr << "FAIL: seek(mid) + read\n"; ++failures; }
        }
    }

    // ── Teste 5: comparar com decodeSlacFile (bit-exact) ────
    {
        // Lê arquivo inteiro via decodeSlacFile.
        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> file_data((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());

        SlacFormat fmt;
        auto full_pcm = decodeSlacFile(file_data, &fmt);

        // Lê via streaming.
        dec.seek_to_sample(0);
        std::vector<std::vector<int32_t>> stream_pcm;
        std::vector<std::vector<int32_t>> chunk;
        while (true) {
            size_t n = dec.read(chunk, 4096);
            if (n == 0) break;
            for (size_t c = 0; c < chunk.size(); ++c) {
                if (c >= stream_pcm.size()) stream_pcm.resize(c + 1);
                stream_pcm[c].insert(stream_pcm[c].end(), chunk[c].begin(), chunk[c].end());
            }
        }

        bool ok = (stream_pcm.size() == full_pcm.size());
        if (ok) {
            for (size_t c = 0; c < stream_pcm.size(); ++c) {
                if (stream_pcm[c].size() != full_pcm[c].size()) { ok = false; break; }
                for (size_t i = 0; i < stream_pcm[c].size(); ++i) {
                    if (stream_pcm[c][i] != full_pcm[c][i]) { ok = false; break; }
                }
                if (!ok) break;
            }
        }

	std::cout << "[DEBUG] full_pcm ch=" << full_pcm.size()
                  << " sizes=";
        for (auto& c : full_pcm) std::cout << c.size() << " ";
        std::cout << "\n[DEBUG] stream_pcm ch=" << stream_pcm.size()
                  << " sizes=";
        for (auto& c : stream_pcm) std::cout << c.size() << " ";
        std::cout << "\n";

        std::cout << "[TEST] streaming vs decodeSlacFile bit-exact=" << (ok ? "yes" : "no") << "\n";
        if (!ok) { std::cerr << "FAIL: streaming vs decodeSlacFile\n"; ++failures; }
    }

    if (failures == 0) {
        std::cout << "\n=== STREAMING DECODER SMOKE TESTS PASSED ===\n";
        return 0;
    }
    std::cerr << "\n=== " << failures << " TEST(S) FAILED ===\n";
    return 1;
}
