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

Tested on Termux (Android, ARM64), 3 tracks, 48kHz stereo:

| Track | Duration | SLAC Size | FLAC Size | Opus 128k |
|-------|----------|-----------|-----------|-----------|
| Raya (electronic) | 1m38s | 73.2% | 72.7% | 8.5% |
| Loser (psych-rock) | 3m43s | 56.5% | 56.1% | 8.6% |
| C&C (dance 90s) | 4m04s | 70.2% | 69.6% | 8.3% |

**SLAC achieves compression parity with FLAC** (~0.5% difference average) while adding spatial metadata, HRIR support, and real-time playback that FLAC doesn't provide.

| Operation | SLAC | FLAC | Opus |
|-----------|------|------|------|
| Encode speed | 2-5 MB/s | 25-30 MB/s | 10-15 MB/s |
| Decode speed | 5-10 MB/s | 25-30 MB/s | 10-15 MB/s |
| Realtime playback | 21x RT | N/A | N/A |

SLAC encode is slower than FLAC due to adaptive decorrelation and spatial analysis, but this is one-time. Decode is still 21x realtime — plenty of headroom for lossless playback.

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
