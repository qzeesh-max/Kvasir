#!/bin/bash
# run_coverage.sh — Build with coverage instrumentation (GCC-16 gcov), run all
# tests, generate line/branch coverage report, and print summary.
set -e

NPROC=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
BUILD_DIR="build_coverage"
REPORT_DIR="coverage_html"

echo "═══════════════════════════════════════════════════"
echo " Kvasir — Coverage Analysis"
echo " Compiler: g++-16   Flags: --coverage"
echo "═══════════════════════════════════════════════════"

rm -rf "$BUILD_DIR" "$REPORT_DIR"
mkdir "$BUILD_DIR"

cd "$BUILD_DIR"
cmake \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-16 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage -g -O0" \
  -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
  .. 2>&1

make -j"$NPROC" 2>&1
cd ..

echo ""
echo "─── Running test suite for coverage data ───"
./build_coverage/tests/kvasir_tests

echo ""
echo "─── Collecting coverage with gcovr ───"
if ! command -v gcovr &>/dev/null; then
  echo "gcovr not found — trying: pip3 install gcovr"
  pip3 install gcovr --quiet
fi

echo ""
echo "─── Line + Branch Summary (include/kvasir only) ───"
gcovr \
  --root . \
  --filter ".*include/kvasir/.*" \
  --exclude-unreachable-branches \
  --exclude-throw-branches \
  --object-directory "$BUILD_DIR" \
  --gcov-executable=/opt/homebrew/bin/gcov-16 \
  --gcov-ignore-parse-errors=negative_hits.warn \
  --sort-uncovered \
  --print-summary 2>&1

echo ""
echo "─── Generating HTML report → $REPORT_DIR ───"
mkdir -p "$REPORT_DIR"
gcovr \
  --root . \
  --filter ".*include/kvasir/.*" \
  --exclude-unreachable-branches \
  --exclude-throw-branches \
  --object-directory "$BUILD_DIR" \
  --gcov-executable=/opt/homebrew/bin/gcov-16 \
  --gcov-ignore-parse-errors=negative_hits.warn \
  --html-details "$REPORT_DIR/index.html" 2>&1

echo ""
echo "═══════════════════════════════════════════════════"
echo " Coverage report: $REPORT_DIR/index.html"
echo "═══════════════════════════════════════════════════"
