#!/usr/bin/env bash
set -e

# Ensure we are built first
./build.sh

echo ""
echo "============================="
echo "Running Kvasir Benchmarks..."
echo "============================="
./build_release/benchmarks/kvasir_benchmarks "$@"
