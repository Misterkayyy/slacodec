#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace slac {

// ──────────────────────────────────────────────────────────────
// Carryless Range Coder (Subbotin style)
//
// Aritmética inteira de 32 bits, sem propagação de carry.
// TOP = 1<<24, BOT = 1<<16.
// ──────────────────────────────────────────────────────────────

static constexpr uint32_t RC_TOP = 1u << 24;
static constexpr uint32_t RC_BOT = 1u << 16;

class RangeEncoder {
public:
    RangeEncoder() : low_(0), range_(0xFFFFFFFFu) {}

    // Codifica símbolo com frequência cumulativa [cum, cum+freq) de [0, total).
    void encode(uint32_t cum, uint32_t freq, uint32_t total) {
        uint32_t r = range_ / total;
        low_ += cum * r;
        range_ = freq * r;
        normalize();
    }

    std::vector<uint8_t> finalize() {
        for (int i = 0; i < 4; ++i) {
            out_.push_back(static_cast<uint8_t>(low_ >> 24));
            low_ <<= 8;
        }
        return std::move(out_);
    }

private:
    uint32_t low_;
    uint32_t range_;
    std::vector<uint8_t> out_;

    void normalize() {
        while ((low_ ^ (low_ + range_)) < RC_TOP || range_ < RC_BOT) {
            if (range_ < RC_BOT && (low_ ^ (low_ + range_)) >= RC_TOP) {
                range_ = static_cast<uint32_t>(-static_cast<int32_t>(low_)) & (RC_BOT - 1);
            }
            out_.push_back(static_cast<uint8_t>(low_ >> 24));
            range_ <<= 8;
            low_ <<= 8;
        }
    }
};

class RangeDecoder {
public:
    RangeDecoder(const uint8_t* data, size_t size)
        : low_(0), range_(0xFFFFFFFFu), code_(0),
          data_(data), size_(size), pos_(0)
    {
        for (int i = 0; i < 4; ++i) {
            code_ = (code_ << 8) | readByte();
        }
    }

    // Retorna a frequência cumulativa decodificada (chamar antes de decode).
    uint32_t getFreq(uint32_t total) {
        range_ /= total;
        uint32_t freq = (code_ - low_) / range_;
        if (freq >= total) freq = total - 1;
        return freq;
    }

    // Atualiza estado após decodificar símbolo [cum, cum+freq).
    void decode(uint32_t cum, uint32_t freq, uint32_t total) {
        (void)total; // range_ já foi dividido em getFreq
        low_ += cum * range_;
        range_ *= freq;
        normalize();
    }

private:
    uint32_t low_;
    uint32_t range_;
    uint32_t code_;
    const uint8_t* data_;
    size_t size_;
    size_t pos_;

    uint8_t readByte() {
        return (pos_ < size_) ? data_[pos_++] : 0;
    }

    void normalize() {
        while ((low_ ^ (low_ + range_)) < RC_TOP || range_ < RC_BOT) {
            if (range_ < RC_BOT && (low_ ^ (low_ + range_)) >= RC_TOP) {
                range_ = static_cast<uint32_t>(-static_cast<int32_t>(low_)) & (RC_BOT - 1);
            }
            low_ <<= 8;
            range_ <<= 8;
            code_ = (code_ << 8) | readByte();
        }
    }
};

// ──────────────────────────────────────────────────────────────
// Modelo adaptativo de bytes (257 símbolos: 0-255 valor, 256 = continua)
//
// Codifica um inteiro unsigned como sequência de "bytes" onde 256
// indica "há mais bytes". Eficiente para valores pequenos (comuns
// em resíduos pós-LPC após fold signed→unsigned).
// ──────────────────────────────────────────────────────────────

class AdaptiveByteModel {
public:
    static constexpr uint32_t NUM_SYM = 257;
    static constexpr uint32_t CONTINUE = 256;

    AdaptiveByteModel() : total_(NUM_SYM) {
        for (uint32_t i = 0; i < NUM_SYM; ++i) freq_[i] = 1;
    }

    void encode(RangeEncoder& enc, uint32_t value) {
        while (value >= CONTINUE) {
            encodeSymbol(enc, CONTINUE);
            value -= CONTINUE;
        }
        encodeSymbol(enc, value);
    }

    uint32_t decode(RangeDecoder& dec) {
        uint32_t value = 0;
        while (true) {
            uint32_t sym = decodeSymbol(dec);
            if (sym == CONTINUE) {
                value += CONTINUE;
            } else {
                value += sym;
                break;
            }
        }
        return value;
    }

private:
    uint32_t freq_[NUM_SYM];
    uint32_t total_;

    void encodeSymbol(RangeEncoder& enc, uint32_t sym) {
        uint32_t cum = 0;
        for (uint32_t i = 0; i < sym; ++i) cum += freq_[i];
        enc.encode(cum, freq_[sym], total_);
        update(sym);
    }

    uint32_t decodeSymbol(RangeDecoder& dec) {
        uint32_t target = dec.getFreq(total_);
        uint32_t cum = 0;
        uint32_t sym = 0;
        for (sym = 0; sym < NUM_SYM; ++sym) {
            if (cum + freq_[sym] > target) break;
            cum += freq_[sym];
        }
        if (sym >= NUM_SYM) sym = NUM_SYM - 1;
        dec.decode(cum, freq_[sym], total_);
        update(sym);
        return sym;
    }

    void update(uint32_t sym) {
        freq_[sym]++;
        total_++;
        if (total_ > 16384) {
            total_ = 0;
            for (uint32_t i = 0; i < NUM_SYM; ++i) {
                freq_[i] = (freq_[i] + 1) >> 1;
                total_ += freq_[i];
            }
        }
    }
};

} // namespace slac
