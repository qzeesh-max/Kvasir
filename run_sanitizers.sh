#!/bin/bash
set -e

echo "Building with Address and Undefined Behavior Sanitizers..."
mkdir -p build_sanitizer
cd build_sanitizer
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1" ..
make -j
cd ..

echo "================================================="
echo "Running unit tests under sanitizers..."
echo "================================================="
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 ./build_sanitizer/tests/kvasir_tests

echo "================================================="
echo "Running benchmarks under sanitizers..."
echo "================================================="
# We use a short min_time because sanitizers can make benchmarks extremely slow
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 ./build_sanitizer/benchmarks/kvasir_benchmarks --benchmark_min_time=0.1

echo "================================================="
echo "All sanitizer checks passed successfully!"
echo "================================================="
