#!/usr/bin/env bash
# ==============================================================================
# LagDaemon Software Research & Development
# Custom Trimmed LLVM Build Script for Ubuntu (WSL2)
# Features: OrcJIT (REPL), WebAssembly Target, Clang (C Interop), LLD Linker
# ==============================================================================

set -e

LLVM_VERSION="18.1.8" # Or main / preferred release branch
WORK_DIR="${HOME}/llvm-build-lagdaemon"
INSTALL_DIR="${HOME}/llvm-lagdaemon-install"
NUM_JOBS=$(nproc)

echo "=== LagDaemon LLVM Build Setup ==="
echo "Building trimmed LLVM version: ${LLVM_VERSION}"
echo "Using ${NUM_JOBS} CPU cores"
echo "Install Destination: ${INSTALL_DIR}"

# Ensure build tools are present
echo "Checking prerequisites..."
sudo apt update
sudo apt install -y build-essential cmake ninja-build git python3 libzstd-dev zlib1g-dev

# Clone LLVM project if src directory doesn't exist
if [ ! -d "${WORK_DIR}/llvm-project" ]; then
    echo "Cloning LLVM project source..."
    git clone --depth 1 --branch "llvmorg-${LLVM_VERSION}" https://github.com/llvm/llvm-project.git "${WORK_DIR}/llvm-project"
else
    echo "LLVM project source directory exists at ${WORK_DIR}/llvm-project"
fi

# Configure CMake
echo "Configuring CMake for trimmed LLVM build..."
mkdir -p "${WORK_DIR}/build"

cmake -G Ninja -S "${WORK_DIR}/llvm-project/llvm" -B "${WORK_DIR}/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DLLVM_TARGETS_TO_BUILD="X86;AArch64;WebAssembly" \
    -DLLVM_ENABLE_PROJECTS="clang;lld" \
    -DLLVM_ENABLE_RTTI=ON \
    -DLLVM_ENABLE_EH=ON \
    -DLLVM_ENABLE_UNWIND=ON \
    -DLLVM_BUILD_EXAMPLES=OFF \
    -DLLVM_BUILD_TESTS=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_INCLUDE_DOCS=OFF \
    -DLLVM_OPTIMIZED_TABLEGEN=ON

# Build and Install
echo "Starting build with Ninja..."
ninja -C "${WORK_DIR}/build" -j"${NUM_JOBS}"

echo "Installing LLVM binaries and headers to ${INSTALL_DIR}..."
ninja -C "${WORK_DIR}/build" install

echo "=== Build Completed Successfully! ==="
echo "Export paths in your ~/.bashrc:"
echo "  export PATH=\"${INSTALL_DIR}/bin:\$PATH\""
echo "  export LLVM_DIR=\"${INSTALL_DIR}/lib/cmake/llvm\""
echo "  export Clang_DIR=\"${INSTALL_DIR}/lib/cmake/clang\""
