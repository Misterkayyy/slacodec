#include "slac/rt/ring_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

using slac::rt::RingBuffer;

int main() {
    int failures = 0;

    // ── Teste 1: capacidade arredondada para potência de 2 ──
    {
        RingBuffer<float> rb(1000);
        size_t cap = rb.capacity();
        bool is_pow2_minus1 = ((cap + 1) & cap) == 0;
        std::cout << "[TEST] capacity=" << cap << " (requested 1000)\n";
        if (!is_pow2_minus1) { std::cerr << "FAIL: capacity not pow2-1\n"; ++failures; }
        if (cap < 1000) { std::cerr << "FAIL: capacity < requested\n"; ++failures; }
    }

    // ── Teste 2: write/read básico (single-thread) ──────────
    {
        RingBuffer<int32_t> rb(256);
        std::vector<int32_t> in = {1, 2, 3, 4, 5, 6, 7, 8};
        std::vector<int32_t> out(8, 0);

        size_t written = rb.write(in.data(), in.size());
        size_t avail = rb.available_to_read();
        size_t read_count = rb.read(out.data(), out.size());

        bool ok = (written == 8) && (avail == 8) && (read_count == 8);
        for (size_t i = 0; i < 8 && ok; ++i) if (out[i] != in[i]) ok = false;

        std::cout << "[TEST] basic write/read ok=" << (ok ? "yes" : "no") << "\n";
        if (!ok) { std::cerr << "FAIL: basic write/read\n"; ++failures; }
    }

    // ── Teste 3: wrap-around (dar a volta no buffer) ────────
    {
        RingBuffer<int32_t> rb(16);
        // Enche e esvazia várias vezes para forçar wrap-around.
        bool ok = true;
        int32_t counter = 0;
        for (int round = 0; round < 10 && ok; ++round) {
            std::vector<int32_t> chunk(10);
            for (auto& v : chunk) v = counter++;
            size_t w = rb.write(chunk.data(), chunk.size());
            if (w != chunk.size()) { ok = false; break; }
            std::vector<int32_t> out(10);
            size_t r = rb.read(out.data(), out.size());
            if (r != chunk.size()) { ok = false; break; }
            for (size_t i = 0; i < chunk.size(); ++i)
                if (out[i] != chunk[i]) { ok = false; break; }
        }
        std::cout << "[TEST] wrap-around ok=" << (ok ? "yes" : "no") << "\n";
        if (!ok) { std::cerr << "FAIL: wrap-around\n"; ++failures; }
    }

    // ── Teste 4: overflow (escrever mais do que cabe) ───────
    {
        RingBuffer<int32_t> rb(8);
        std::vector<int32_t> big(100, 42);
        size_t written = rb.write(big.data(), big.size());
        bool ok = (written <= rb.capacity());
        std::cout << "[TEST] overflow written=" << written << "/" << big.size()
                  << " cap=" << rb.capacity() << "\n";
        if (!ok) { std::cerr << "FAIL: overflow\n"; ++failures; }
    }

    // ── Teste 5: underflow (ler mais do que há) ─────────────
    {
        RingBuffer<int32_t> rb(16);
        std::vector<int32_t> small = {1, 2, 3};
        rb.write(small.data(), small.size());
        std::vector<int32_t> out(10, 0);
        size_t read_count = rb.read(out.data(), out.size());
        bool ok = (read_count == 3) && (out[0] == 1) && (out[2] == 3);
        std::cout << "[TEST] underflow read=" << read_count << "\n";
        if (!ok) { std::cerr << "FAIL: underflow\n"; ++failures; }
    }

    // ── Teste 6: produtor/consumidor multi-thread ───────────
    {
        RingBuffer<int32_t> rb(1024);
        const int32_t total = 100000;
        std::atomic<bool> done{false};
        bool data_ok = true;

        std::thread producer([&]() {
            int32_t val = 0;
            int32_t produced = 0;
            while (produced < total) {
                int32_t batch[64];
                int n = 0;
                while (n < 64 && produced < total) {
                    batch[n++] = val++;
                    ++produced;
                }
                size_t written = 0;
                size_t offset = 0;
                while (offset < static_cast<size_t>(n)) {
                    written = rb.write(batch + offset, n - offset);
                    offset += written;
                    if (written == 0) std::this_thread::yield();
                }
            }
            done.store(true, std::memory_order_release);
        });

        std::thread consumer([&]() {
            int32_t expected = 0;
            int32_t consumed = 0;
            while (consumed < total) {
                int32_t batch[64];
                size_t r = rb.read(batch, 64);
                for (size_t i = 0; i < r; ++i) {
                    if (batch[i] != expected) { data_ok = false; }
                    ++expected;
                }
                consumed += static_cast<int32_t>(r);
                if (r == 0) std::this_thread::yield();
            }
        });

        producer.join();
        consumer.join();

        std::cout << "[TEST] multithread SPSC data_ok=" << (data_ok ? "yes" : "no")
                  << " (" << total << " samples)\n";
        if (!data_ok) { std::cerr << "FAIL: multithread SPSC\n"; ++failures; }
    }

    if (failures == 0) {
        std::cout << "\n=== RING BUFFER SMOKE TESTS PASSED ===\n";
        return 0;
    }
    std::cerr << "\n=== " << failures << " TEST(S) FAILED ===\n";
    return 1;
}
