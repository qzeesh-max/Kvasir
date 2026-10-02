#!/bin/bash
# run_tsan.sh — Build and run tests under ThreadSanitizer (clang only on macOS).
# Uses clang++ because GCC-16 TSAN runtime does not link on macOS ARM.
set -e

NPROC=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
BUILD_DIR="build_tsan"

echo "═══════════════════════════════════════════════════"
echo " Kvasir — ThreadSanitizer Test Run"
echo " Compiler: clang++   Flags: -fsanitize=thread"
echo "═══════════════════════════════════════════════════"

rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

cmake \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-std=c++26 -fsanitize=thread -fno-omit-frame-pointer -g -O1" \
  .. 2>&1

make -j"$NPROC" 2>&1
cd ..

echo ""
echo "─── Running tests under TSAN ───"
TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1:history_size=4" \
  ./build_tsan/tests/kvasir_tests \
  --gtest_filter="TsanPool*:TsanAdvanced*" \
  --gtest_repeat=3 \
  --gtest_shuffle

echo ""
echo "─── Running all tests under TSAN (smoke) ───"
TSAN_OPTIONS="halt_on_error=1:history_size=4" \
  ./build_tsan/tests/kvasir_tests

echo ""
echo "═══════════════════════════════════════════════════"
echo " All TSAN checks passed!"
echo "═══════════════════════════════════════════════════"
