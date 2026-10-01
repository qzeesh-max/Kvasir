#!/usr/bin/env bash
set -e

BUILD_DIR="build_release"

echo "Configuring and building Kvasir in $BUILD_DIR..."

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -DCMAKE_BUILD_TYPE=Release ..
make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

echo "Build successful."
