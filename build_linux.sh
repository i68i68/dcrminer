#!/usr/bin/env bash
set -e

echo "==================================================="
echo "  Building DCR BLAKE3 Miner for Linux (Ubuntu/Debian)"
echo "==================================================="

# Verify requirements
command -v cmake >/dev/null 2>&1 || { echo "❌ CMake is required. Install: sudo apt install -y cmake"; exit 1; }

BUILD_DIR="build_linux"
mkdir -p "$BUILD_DIR"

CUDA_OPT="OFF"
if command -v nvcc >/dev/null 2>&1; then
    echo "🟢 NVIDIA CUDA Toolkit detected (nvcc found). Enabling CUDA acceleration."
    CUDA_OPT="ON"
else
    echo "ℹ️  nvcc not found. Compiling optimized CPU SIMD (AVX-512/AVX2) and OpenCL."
fi

# Detect Ninja if available
GEN_FLAG=""
if command -v ninja >/dev/null 2>&1; then
    GEN_FLAG="-G Ninja"
fi

cmake -B "$BUILD_DIR" $GEN_FLAG \
    -DCMAKE_BUILD_TYPE=Release \
    -DDCR_ENABLE_CUDA="$CUDA_OPT" \
    -DDCR_ENABLE_OPENCL=ON

if command -v ninja >/dev/null 2>&1; then
    ninja -C "$BUILD_DIR"
else
    cmake --build "$BUILD_DIR" --config Release -j"$(nproc)"
fi

echo ""
echo "==================================================="
echo "✅ Build complete! Binary: $BUILD_DIR/dcrminer"
echo "Run: ./$BUILD_DIR/dcrminer --help"
echo "==================================================="
