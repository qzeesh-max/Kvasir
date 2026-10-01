#!/bin/bash
set -e

mkdir -p build_coverage
cd build_coverage
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" ..
make -j
cd ..

LLVM_PROFILE_FILE="coverage.profraw" ./build_coverage/tests/kvasir_tests
xcrun llvm-profdata merge -sparse coverage.profraw -o coverage.profdata

echo "================ SUMMARY ================"
xcrun llvm-cov report ./build_coverage/tests/kvasir_tests -instr-profile=coverage.profdata -ignore-filename-regex="(_deps|tests|/usr/)"

xcrun llvm-cov show ./build_coverage/tests/kvasir_tests -instr-profile=coverage.profdata -ignore-filename-regex="(_deps|tests|/usr/)" > coverage_report.txt
