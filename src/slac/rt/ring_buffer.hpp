#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace slac::rt {

// ──────────────────────────────────────────────────────────────
// Ring buffer lock-free SPSC (single-producer / single-consumer).
//
// - Capacidade arredondada para potência de 2 (máscara rápida).
// - Sem mutexes: apenas std::atomic com memory ordering adequado.
// - Uso típico: decoder escreve (write), audio callback lê (read).
// ──────────────────────────────────────────────────────────────
template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) {
        // Arredonda para potência de 2 e adiciona folga.
        size_t cap = 1;
        while (cap < capacity) cap <<= 1;
        cap <<= 1;  // dobra para garantir espaço útil >= capacity
        buffer_.resize(cap);
        mask_ = cap - 1;
        write_pos_.store(0, std::memory_order_relaxed);
        read_pos_.store(0, std::memory_order_relaxed);
    }

    // Capacidade útil (amostras que cabem).
    size_t capacity() const { return buffer_.size() - 1; }

    // Quantos itens disponíveis para leitura.
    size_t available_to_read() const {
        size_t w = write_pos_.load(std::memory_order_acquire);
        size_t r = read_pos_.load(std::memory_order_acquire);
        return (w - r) & mask_;
    }

    // Quanto espaço livre para escrita.
    size_t available_to_write() const {
        return capacity() - available_to_read();
    }

    // Escreve até `count` itens. Retorna quantos foram realmente escritos.
    // (Chamar apenas na thread produtora.)
    size_t write(const T* data, size_t count) {
        size_t w = write_pos_.load(std::memory_order_relaxed);
        size_t r = read_pos_.load(std::memory_order_acquire);
        size_t free_space = (r - w - 1) & mask_;
        size_t to_write = (count < free_space) ? count : free_space;

        for (size_t i = 0; i < to_write; ++i) {
            buffer_[(w + i) & mask_] = data[i];
        }

        write_pos_.store((w + to_write) & mask_, std::memory_order_release);
        return to_write;
    }

    // Lê até `count` itens. Retorna quantos foram realmente lidos.
    // (Chamar apenas na thread consumidora.)
    size_t read(T* data, size_t count) {
        size_t r = read_pos_.load(std::memory_order_relaxed);
        size_t w = write_pos_.load(std::memory_order_acquire);
        size_t avail = (w - r) & mask_;
        size_t to_read = (count < avail) ? count : avail;

        for (size_t i = 0; i < to_read; ++i) {
            data[i] = buffer_[(r + i) & mask_];
        }

        read_pos_.store((r + to_read) & mask_, std::memory_order_release);
        return to_read;
    }

    // Esvazia o buffer. (Chamar apenas quando não há produtor/consumidor ativos.)
    void reset() {
        write_pos_.store(0, std::memory_order_relaxed);
        read_pos_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<T> buffer_;
    size_t mask_ = 0;
    // Cachelines separadas para evitar false sharing.
    alignas(64) std::atomic<size_t> write_pos_;
    alignas(64) std::atomic<size_t> read_pos_;
};

} // namespace slac::rt
