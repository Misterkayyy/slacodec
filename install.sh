#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# SLACodec build script
# Uso: ./install.sh [--clean] [--debug] [--jobs N]
# ═══════════════════════════════════════════════════════════════════════════
set -e

JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
BUILD_TYPE="Release"
CLEAN=0

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
            CLEAN=1
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --jobs)
            JOBS="${2:-$JOBS}"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: ./install.sh [--clean] [--debug] [--jobs N]"
            exit 1
            ;;
    esac
done

# Detectar plataforma/generator
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || -n "$MSYSTEM" ]]; then
    GENERATOR="MinGW Makefiles"
    EXT=".exe"
else
    GENERATOR="Unix Makefiles"
    EXT=""
fi

# Clean se pedido
if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning build directory..."
    rm -rf build
fi

# Configurar
echo "Configuring SLACodec (type=$BUILD_TYPE, jobs=$JOBS)..."
cmake -S . -B build -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# Build
echo "Building..."
cmake --build build -j "$JOBS"

echo ""
echo "Build complete!"
echo "CLI:    ./build/slacodec-cli$EXT"
echo "Tag:    ./build/slacodec-tag$EXT"