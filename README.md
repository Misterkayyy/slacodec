# SLACodec — Spatial Lossless Audio Codec

A production-ready lossless audio codec with **integrated spatial processing**, **automatic HRIR embedding**, and **real-time, low-latency playback**.

## Features

### Core Codec
- **True lossless compression** — bit-perfect decode with SHA-256 integrity verification.
- **Adaptive stereo decorrelation** — dynamically chooses the optimal mode (L/R, M/S, L/S, or R/S) per frame.
- **LPC + Rice entropy coding** — efficient linear prediction and residual compression.
- **Frame-based seek table** — instant, sample-accurate random access to any point in the track.
- **Encoding presets** — `fast`, `balanced`, `high`, and `extreme` to tune speed vs. compression ratio.

### Spatial Processing
- **Embedded HRIR support** — automatically analyzes audio, selects the optimal Impulse Response, and embeds it directly into the `.slac` container.
- **Spatial metadata** — `spat` chunk stores wideness, reverb presets, and processing flags.
- **Dynamic automation** — `auto` chunk provides sample-accurate wideness and reverb wet modulation curves.
- **Advanced DSP chain** — True-stereo HRIR convolution (2 or 4 channels) + 8-tap FDN reverb with adaptive damping.
- **Smart makeup gain** — automatically recovers volume lost during HRIR unity-gain normalization.

### Real-time Playback
- **Cross-platform audio engines** — Event-driven WASAPI (Windows) and low-latency AAudio (Android API 26+).
- **Streaming architecture** — frame-by-frame decoding with no full-file memory loading.
- **Lock-free ring buffer** — seamless SPSC bridge between the decode producer and audio consumer threads.
- **Per-block spatial processing** — processes audio in small blocks (e.g., 128 samples) for sub-3ms latency.

---

## Usage

### Encode
```bash
slacodec-cli encode input.wav output.slac [options]
```
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
slacodec-cli decode input.slac output.wav [options]
```
**Options:**
- `--spatial` : Apply the full spatial DSP chain (widening -> HRIR -> reverb)
- `--hrir <path>` : Override with a custom true-stereo HRIR (4-channel WAV)
- `--ir <path>` : Override with a custom convolution reverb IR

### Real-time Playback
```bash
slacodec-cli play input.slac [options]
```
**Options:**
- `--spatial` : Apply the embedded spatial chain
- `--hrir <path>` : Override with a custom true-stereo HRIR
- `--makeup-db <N>` : Volume boost post-chain (default: 6 dB when HRIR is active)
- `--limit-mode <mode>` : `normalize` | `loudness` | `limit` | `none`

### Verify Losslessness
```bash
slacodec-cli verify original.wav encoded.slac
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
    │ (AAudio/WASAPI callback, consumer thread)
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

## Build

### Linux / macOS / Termux (Android)
```bash
./install.sh          # Release build
./install.sh --clean  # Clean build directory
./install.sh --debug  # Debug build with symbols
```

### Manual CMake
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
*Requires: C++17 compiler, pthreads, and AAudio headers (for Android targets).*

---

## Roadmap & Status

### Completed (v1.0.0)
- [x] Core lossless codec (LPC + Rice)
- [x] Container format with metadata and integrity chunks
- [x] Adaptive stereo decorrelation (M/S, L/S, R/S)
- [x] Auto-spatial analysis with dynamic keyframe generation
- [x] **True-stereo HRIR convolution with automatic selection and embedding**
- [x] FDN reverb with adaptive damping
- [x] Streaming decoder with lock-free ring buffer (SPSC)
- [x] Real-time playback (Windows WASAPI + Android AAudio)
- [x] Bit-exact verification with SHA-256

### Future Enhancements
- [ ] Context-adaptive Rice coding for improved compression ratios
- [ ] SIMD optimization (Runtime dispatch for AVX2 / ARM NEON)
- [ ] Advanced loudness normalization (LUFS) for consistent volume across tracks
- [ ] Full native Android App UI integration (MediaSession, Background Service)