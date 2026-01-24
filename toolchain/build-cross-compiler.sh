#!/bin/bash
# Build x86_64-elf cross-compiler for Kurios2
# Run this if you want a dedicated cross-compiler instead of using system GCC

set -e

# Configuration
export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"

BINUTILS_VERSION=2.42
GCC_VERSION=13.2.0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/cross-build"
SRC_DIR="$BUILD_DIR/src"

echo "=== Kurios2 Cross-Compiler Build Script ==="
echo "Target: $TARGET"
echo "Prefix: $PREFIX"
echo ""

# Check dependencies
echo "Checking dependencies..."
for cmd in gcc g++ make bison flex libgmp-dev libmpc-dev libmpfr-dev texinfo; do
    if ! command -v $cmd &> /dev/null && ! dpkg -l | grep -q "^ii  $cmd"; then
        echo "Missing: $cmd"
        echo "Install with: sudo apt-get install build-essential bison flex libgmp-dev libmpc-dev libmpfr-dev texinfo"
        exit 1
    fi
done

mkdir -p "$BUILD_DIR" "$SRC_DIR" "$PREFIX"

cd "$SRC_DIR"

# Download sources
if [ ! -f "binutils-$BINUTILS_VERSION.tar.xz" ]; then
    echo "Downloading binutils $BINUTILS_VERSION..."
    wget "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz"
fi

if [ ! -f "gcc-$GCC_VERSION.tar.xz" ]; then
    echo "Downloading GCC $GCC_VERSION..."
    wget "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz"
fi

# Extract
echo "Extracting sources..."
tar -xf "binutils-$BINUTILS_VERSION.tar.xz" 2>/dev/null || true
tar -xf "gcc-$GCC_VERSION.tar.xz" 2>/dev/null || true

# Build binutils
echo "Building binutils..."
mkdir -p "$BUILD_DIR/build-binutils"
cd "$BUILD_DIR/build-binutils"
"$SRC_DIR/binutils-$BINUTILS_VERSION/configure" \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror
make -j$(nproc)
make install

# Build GCC
echo "Building GCC (this takes a while)..."
mkdir -p "$BUILD_DIR/build-gcc"
cd "$BUILD_DIR/build-gcc"
"$SRC_DIR/gcc-$GCC_VERSION/configure" \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c,c++ \
    --without-headers
make -j$(nproc) all-gcc
make -j$(nproc) all-target-libgcc
make install-gcc
make install-target-libgcc

echo ""
echo "=== Cross-compiler built successfully! ==="
echo ""
echo "Add to your PATH:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo ""
echo "Update toolchain/config.mk:"
echo "  CROSS_PREFIX := x86_64-elf-"
echo ""
echo "Verify:"
echo "  x86_64-elf-gcc --version"
