#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
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
            echo "  --install   Install globally (handles Linux, macOS, Termux, Windows)"
            exit 1
            ;;
    esac
done

# Detectar plataforma
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || -n "$MSYSTEM" ]]; then
    GENERATOR="MinGW Makefiles"
    EXT=".exe"
    PLATFORM="Windows"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    GENERATOR="Unix Makefiles"
    EXT=""
    PLATFORM="macOS"
elif [ -n "$TERMUX_VERSION" ]; then
    GENERATOR="Unix Makefiles"
    EXT=""
    PLATFORM="Termux"
else
    GENERATOR="Unix Makefiles"
    EXT=""
    PLATFORM="Linux"
fi

echo "🖥️  Detected Platform: $PLATFORM"

# Clean se pedido
if [[ $CLEAN -eq 1 ]]; then
    echo "🧹 Cleaning build directory..."
    rm -rf build
fi

# Configurar
echo "⚙️  Configuring SLACodec (type=$BUILD_TYPE, jobs=$JOBS)..."
cmake -S . -B build -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# Build
echo "🔨 Building..."
cmake --build build -j "$JOBS"

echo ""
echo "✅ Build complete!"
echo ""
echo "Binaries:"
echo "  CLI:  ./build/slacodec$EXT"
echo ""

# Lógica de Instalação Inteligente
if [[ $DO_INSTALL -eq 1 ]]; then
    echo "📦 Attempting global installation..."
    
    if [[ "$PLATFORM" == "Windows" ]]; then
        echo "⚠️  Global installation not supported on Windows via this script."
        echo "   Tip: Copy ./build/slacodec.exe to a folder in your system PATH (e.g., C:\\Windows\\System32 or a custom bin folder)."
    
    elif [[ "$PLATFORM" == "Termux" ]]; then
        echo "📱 Termux detected. Installing to \$PREFIX/bin ($PREFIX/bin)..."
        cp build/slacodec "$PREFIX/bin/slacodec"
        chmod +x "$PREFIX/bin/slacodec"
        echo "✅ Binary installation complete!"
        
        # Instalar IRs no Termux
        echo "📱 Installing IRs to \$PREFIX/share/slacodec/ir..."
        mkdir -p "$PREFIX/share/slacodec/ir"
        cp -r ir/* "$PREFIX/share/slacodec/ir/" 2>/dev/null || echo "⚠️  No IR files found in ./ir/"
        echo "✅ IR installation complete! You can now run 'slacodec' from anywhere in Termux."
    
    else
        # Linux / macOS padrão
        echo "🐧 Installing to /usr/local/bin..."
        echo "   (requires sudo password)"
        sudo cmake --install build
        echo "✅ Binary installation complete!"
        
        # Instalar IRs no Linux/macOS
        echo "📦 Installing IRs to /usr/local/share/slacodec/ir..."
        sudo mkdir -p /usr/local/share/slacodec/ir
        sudo cp -r ir/* /usr/local/share/slacodec/ir/ 2>/dev/null || echo "⚠️  No IR files found in ./ir/"
        echo "✅ IR installation complete! You can now run 'slacodec' from anywhere."
    fi
fi