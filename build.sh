#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════
# SLACodec build script
# Uso: ./build.sh [--clean] [--debug] [--jobs N] [--install]
# ═══════════════════════════════════════════════════════════════════════════
set -e

JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
BUILD_TYPE="Release"
CLEAN=0
DO_INSTALL=0

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
        --install)
            DO_INSTALL=1
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: ./install.sh [--clean] [--debug] [--jobs N] [--install]"
            echo ""
            echo "Options:"
            echo "  --clean     Remove build directory before building"
            echo "  --debug     Build in Debug mode (default: Release)"
            echo "  --jobs N    Number of parallel jobs (default: auto-detect)"
            echo "  --install   Install globally to /usr/local/bin (requires sudo)"
            exit 1
            ;;
    esac
done

# Detectar plataforma/generator
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || -n "$MSYSTEM" ]]; then
    GENERATOR="MinGW Makefiles"
    EXT=".exe"
    echo "🪟 Windows (MinGW) detected"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    GENERATOR="Unix Makefiles"
    EXT=""
    echo "🍎 macOS detected"
else
    GENERATOR="Unix Makefiles"
    EXT=""
    echo "🐧 Linux detected"
fi

# Clean se pedido
if [[ $CLEAN -eq 1 ]]; then
    echo " Cleaning build directory..."
    rm -rf build
fi

# Configurar
echo "️  Configuring SLACodec (type=$BUILD_TYPE, jobs=$JOBS)..."
cmake -S . -B build -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# Build
echo "🔨 Building..."
cmake --build build -j "$JOBS"

echo ""
echo "✅ Build complete!"
echo ""
echo "Binaries:"
echo "  CLI:  ./build/slacodec$EXT"
echo "  Tag:  ./build/slacodec-tag$EXT"
echo ""

# Install se pedido
if [[ $DO_INSTALL -eq 1 ]]; then
    if [[ "$EXT" != "" ]]; then
        echo "⚠️  Global installation not supported on Windows."
        echo "   Please add ./build to your PATH manually."
    else
        echo "📦 Installing globally to /usr/local/bin..."
        echo "   (requires sudo password)"
        sudo cmake --install build
        echo ""
        echo "✅ Installation complete!"
        echo "   You can now run 'slacodec' from anywhere."
        echo ""
        echo "📝 Quick test:"
        echo "   slacodec --version"
        echo "   slacodec encode input.wav output.slac --auto-spatial"
    fi
fi