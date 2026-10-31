# SLACodec — Spatial Lossless Audio Codec (v1.1.0)

A production-ready lossless audio codec with **integrated spatial processing**, **automatic HRIR embedding**, and **real-time, low-latency playback**.

## Features

### Core Codec
- **True lossless compression** — bit-perfect decode with SHA-256 integrity verification.
- **Adaptive stereo decorrelation** — dynamically chooses the optimal mode (L/R, M/S, L/S, or R/S) per frame.
- **LPC + Rice entropy coding** — efficient linear prediction and residual compression.
- **Frame-based seek table** — instant, sample-accurate random access to any point in the track.
- **High-performance encoding** — multi-threaded (up to 8 threads) with zero-allocation loops for maximum speed.

### Spatial Processing
- **Embedded HRIR support** — automatically analyzes audio, selects the optimal Impulse Response, and embeds it directly into the `.slac` container.
- **Spatial metadata** — `spat` chunk stores wideness, reverb presets, and processing flags.
- **Context-aware dynamic automation** — `auto` chunk provides sample-accurate wideness and reverb wet modulation (e.g., boosts reverb during panning or quiet passages).
- **Advanced DSP chain** — True-stereo HRIR convolution (2 or 4 channels) + 8-tap FDN reverb with adaptive damping.
- **Smart makeup gain** — automatically recovers volume lost during HRIR unity-gain normalization.

### Real-time Playback
- **Cross-platform audio engines** — Event-driven WASAPI (Windows), low-latency AAudio (Android API 26+), and native ALSA (Linux).
- **Streaming architecture** — frame-by-frame decoding with no full-file memory loading.
- **Lock-free ring buffer** — seamless SPSC bridge between the decode producer and audio consumer threads.
- **Per-block spatial processing** — processes audio in small blocks (e.g., 128 samples) for sub-3ms latency.

---

## Usage

### Encode
```bash
slacodec encode <input> [output.slac] [options]
```
*(Note: If `output.slac` is omitted, it automatically generates a file with the `.slac` extension).*

**Options:**
- `--preset-encode <name>` : `fast` | `balanced` | `high` | `extreme`
- `--adaptive` : Force adaptive stereo decorrelation
- `--fixed-ms` : Force fixed Mid/Side decorrelation
- `--auto-spatial` : Auto-derive spatial params and **automatically embed the optimal HRIR**
- `--wideness <0-150>` : Stereo wideness % (default: 100)
- `--reverb-wet <0-100>` : Reverb wet % (default: 0)
- `--preset <0-255>` : Reverb preset ID

### Decode
```bash
slacodec decode <input.slac> <output.wav> [options]
```
**Options:**
- `--spatial` : Apply the full spatial DSP chain (widening -> HRIR -> reverb)
- `--hrir <path>` : Override with a custom true-stereo HRIR (4-channel WAV)
- `--ir <path>` : Override with a custom convolution reverb IR

### Real-time Playback
```bash
slacodec play <input.slac> [options]
```
**Options:**
- `--spatial` : Apply the embedded spatial chain (Required to activate embedded HRIR)
- `--hrir <path>` : Override with a custom true-stereo HRIR
- `--makeup-db <N>` : Volume boost post-chain (default: 6 dB when HRIR is active)
- `--limit-mode <mode>` : `normalize` | `loudness` | `limit` | `none`

### Verify Losslessness
```bash
slacodec verify <original.wav> <encoded.slac>
```
*Decodes the file in memory and performs a bit-exact comparison against the original, including SHA-256 hash validation.*

---

## Architecture

```text
Encoder Pipeline:
  WAV/Source -> LPC Analysis -> Adaptive Decorrelation -> Rice Coding -> SLAC Container (+ HIRR Embedding)

Decoder Pipeline (Real-time):
  SLAC File 
    │ (StreamingDecoder, producer thread)
    ▼
  Lock-free Ring Buffer (SPSC)
    │ (AAudio/WASAPI/ALSA callback, consumer thread)
    ▼
  Block Processor (Spatial chain: Wideness -> HRIR Convolution -> FDN Reverb -> Limiter)
    │
    ▼
  Speakers / Headphones
```

---

## Container Format

| Chunk | Purpose |
|-------|---------|
| `SLAC` | Magic number + version |
| `fmt`  | Sample rate, channels, bit depth, decorrelation mode |
| `spat` | Spatial metadata (wideness, reverb preset, flags) |
| `auto` | Automation keyframes (wideness + reverb wet curves) |
| `seek` | Per-frame byte offsets for instant seeking |
| `HIRR` | **Embedded HRIR data** (float32, 2 or 4 channels) |
| `data` | Compressed audio frames |
| `hash` | SHA-256 of decoded PCM (integrity check) |

---

## Build & Install

### Linux / macOS / Termux (Android)
```bash
./install.sh --install       # Release build and install globally to /usr/local/bin
./install.sh --clean         # Clean build directory before building
./install.sh --debug         # Debug build with symbols
```

### Manual CMake
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build   # Linux/macOS only
```
*Requires: C++17 compiler, pthreads, and AAudio headers (for Android) or libasound2-dev (for Linux).*

---

## Roadmap & Status

### Completed (v1.1.0)
- [x] Core lossless codec (LPC + Rice)
- [x] Container format with metadata and integrity chunks
- [x] Adaptive stereo decorrelation (M/S, L/S, R/S)
- [x] Context-aware auto-spatial analysis with dynamic keyframe generation
- [x] **True-stereo HRIR convolution with automatic selection and embedding**
- [x] FDN reverb with adaptive damping
- [x] Streaming decoder with lock-free ring buffer (SPSC)
- [x] Real-time playback (Windows WASAPI + Android AAudio + **Linux ALSA**)
- [x] Bit-exact verification with SHA-256
- [x] Encoder performance optimization (Multi-threading + Zero-allocation)

### Future Enhancements
- [ ] Context-adaptive Rice coding for improved compression ratios
- [ ] SIMD optimization (Runtime dispatch for AVX2 / ARM NEON)
- [ ] Advanced loudness normalization (LUFS) for consistent volume across tracks
- [ ] Full native Android App UI integration (MediaSession, Background Service)

## 📜 Credits and Data Attribution

This project utilizes spatial audio Impulse Responses (IRs) derived and modified from the **SADIE II** database, developed by the AudioLab at the University of York.

- **Original Source**: [SADIE II Database](https://www.york.ac.uk/sadie-project/database.html)
- **Original Data License**: [Apache License, Version 2.0](https://www.apache.org/licenses/LICENSE-2.0)
- **Copyright**: © 2018 University of York

If you use this project in academic research, please also cite the original dataset paper:  
> Armstrong, C., et al. (2018). "A Perceptual Evaluation of Individual and Non-Individual HRTFs: A Case Study of the SADIE II Database." *Applied Sciences*, 8(11), 2029. [DOI: 10.3390/app8112029](https://doi.org/10.3390/app8112029)