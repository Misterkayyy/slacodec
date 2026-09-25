# SLACodec — Spatial Lossless Audio Codec

A lossless audio codec with **integrated spatial processing** and **real-time playback on Android**.

## Features

### Core Codec
- **True lossless compression** — bit-perfect decode via SHA-256 verification
- **Adaptive stereo decorrelation** — automatically chooses L/R, M/S, L/S, or R/S per frame
- **LPC + Rice entropy coding** — efficient prediction + residual compression
- **Frame-based seek table** — instant random access to any point in the track
- **Multiple presets** — fast / balanced / high / extreme (tune speed vs compression)

### Spatial Processing
- **Spatial metadata embedded in the file** — spat chunk stores wideness, reverb preset, automation curves
- **True-stereo HRIR support** — 4-channel (LL, LR, RL, RR) impulse responses for realistic spatialization
- **FDN reverb** — 8-tap feedback delay network with adaptive damping
- **Automation curves** — sample-accurate wideness and reverb wet modulation
- **Smart makeup gain** — recovers volume lost to HRIR unity-gain normalization (6 dB default)
- **Auto-spatial analysis** — derive optimal spatial parameters from the audio itself

### Real-time Playback (Android)
- **AAudio low-latency callback** — API 26+ with PerformanceMode::LowLatency
- **Streaming decoder** — reads frames on demand, no full file load
- **Lock-free ring buffer** — bridges producer (decode) and consumer (audio callback) threads
- **Per-block spatial processing** — 128-sample blocks for sub-3ms latency
- **21x realtime throughput** — validated on mid-range Android device

## Benchmark Results

Tested on Termux (Android, ARM64, quad-core), 3 tracks, 48kHz stereo:

### Compression Ratio

| Track | Duration | WAV Size | SLAC Extreme | FLAC -8 | Opus 128k |
|-------|----------|----------|--------------|---------|-----------|
| Raya (electronic) | 1m38s | 19 MB | 14 MB (73.7%) | 13 MB (68.4%) | 1.5 MB (8.5%) |
| Loser (psych-rock) | 3m43s | 42 MB | 22 MB (52.4%) | 22 MB (52.4%) | 3.5 MB (8.6%) |
| C&C (dance 90s) | 4m04s | 46 MB | 32 MB (69.6%) | 31 MB (67.4%) | 3.7 MB (8.3%) |

**SLAC Extreme achieves compression within 2-5% of FLAC -8** (maximum compression level) while adding spatial metadata, HRIR support, and real-time playback that FLAC doesn't provide.

- **Loser**: identical compression (52.4%)
- **C&C**: 2.2% larger than FLAC -8
- **Raya**: 5.3% larger than FLAC -8

The difference is small and acceptable considering FLAC has 20+ years of optimization with hand-tuned assembly, while SLAC adds spatial processing capabilities.

### Encoding Performance

With parallelization across 4 threads on ARM64 quad-core:

| Track | SLAC Extreme (parallel) | FLAC -8 | Speedup vs sequential |
|-------|------------------------|---------|----------------------|
| Raya | 4.1s | 0.73s | 2.4x |
| Loser | 8.6s | 1.62s | 2.4x |
| C&C | 9.9s | 1.78s | 2.4x |

SLAC encode is ~5x slower than FLAC due to adaptive decorrelation, spatial analysis, and higher LPC order (16 vs FLAC's optimized predictors). However, encoding is one-time, and the 2.4x parallelization speedup makes it practical for batch processing.

### Real-time Playback

| Metric | Value |
|--------|-------|
| Decode throughput | 21x realtime |
| Spatial processing latency | <3ms (128-sample blocks) |
| HRIR convolution | Partitioned (FFT-based) |
| Lock-free audio callback | Yes (SPSC ring buffer) |

Decode speed is 21x realtime — more than enough headroom for lossless playback with spatial processing on mid-range Android devices.

## Usage

### Encode

    slacodec-cli encode input.wav output.slac [options]

    Options:
      --preset-encode <name>   fast|balanced|high|extreme
      --adaptive               Force adaptive stereo decorrelation
      --fixed-ms               Force fixed M/S
      --auto-spatial           Auto-derive spatial params from audio
      --wideness <0-150>       Stereo wideness % (default 100)
      --reverb-wet <0-100>     Reverb wet % (default 0)
      --preset <0-255>         Reverb preset ID

### Decode

    slacodec-cli decode input.slac output.wav [options]

    Options:
      --spatial                Apply spatial DSP chain (widening -> HRIR -> reverb)
      --hrir <path>            Apply true-stereo HRIR (4-channel WAV)
      --ir <path>              Convolution reverb IR

### Real-time Playback

    slacodec-cli play input.slac [options]

    Options:
      --spatial                Apply spatial chain
      --hrir <path>            True-stereo HRIR
      --makeup-db <N>          Volume boost post-chain (default 6 dB with HRIR)

### Verify losslessness

    slacodec-cli verify original.wav encoded.slac

## Architecture

    Encoder pipeline:
      WAV -> LPC analysis -> Adaptive decorrelation -> Rice coding -> SLAC container

    Decoder pipeline (offline):
      SLAC -> Rice decode -> Inverse decorrelation -> LPC synthesis -> WAV

    Decoder pipeline (real-time):
      SLAC file
        | (StreamingDecoder, producer thread)
      RingBuffer (lock-free SPSC)
        | (AAudio callback, consumer thread)
      BlockProcessor (spatial chain per 128 samples)
        |
      Speakers / headphones

## Container Format

| Chunk | Purpose |
|-------|---------|
| SLAC | Magic + version |
| fmt  | Sample rate, channels, bit depth, decorrelation mode |
| spat | Spatial metadata (wideness, reverb preset, flags) |
| auto | Automation keyframes (wideness + reverb wet curves) |
| seek | Per-frame byte offsets for instant seeking |
| data | Compressed audio frames |
| hash | SHA-256 of decoded PCM (integrity check) |

## Build

    cmake -S . -B build
    cmake --build build

Requires C++17, pthreads, AAudio (Android API 26+).

## Roadmap

- [x] Core lossless codec (LPC + Rice)
- [x] Container format with metadata chunks
- [x] Adaptive stereo decorrelation (M/S, L/S, R/S)
- [x] Spatial metadata embedding (spat + auto chunks)
- [x] True-stereo HRIR support (4-channel IR)
- [x] FDN reverb with adaptive damping
- [x] Streaming decoder (no full file load)
- [x] Lock-free ring buffer (SPSC)
- [x] Per-block spatial processing (128 samples)
- [x] AAudio real-time playback (Android)
- [x] Smart makeup gain (6 dB default with HRIR)
- [ ] Extreme preset benchmark (compare SLAC extreme vs FLAC)
- [ ] LPC optimization (Levinson-Durbin, NEON SIMD)
- [ ] Loudness normalization (LUFS) for consistent volume across tracks
- [ ] Android app (JNI + UI)
