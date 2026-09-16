# SLACodec

Spatial Lossless Audio Codec.

Codec lossless de PCM (`.slac`) que carrega no arquivo a intenção de mixagem espacial, sem carregar o áudio processado. O decoder aplica o DSP espacial: widening, HRIR e reverb.

## Perfis

- `SLAC Core`: somente lossless.
- `SLAC Spatial`: lossless + metadados/parâmetros espaciais.

## Roadmap resumido

- F1: MVP lossless + spatial offline.
- F2: decode em tempo real.
- F3: adaptação automática / extreme.
- F4: multicanal + plugins DAW.
- F5: spec pública + test vectors.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Testes

```bash
./golden.sh make
./golden.sh check
```

## Observações

Projeto atualmente desenvolvido em Termux/Android.
