#!/usr/bin/env bash
set -e

# Ensure we are built first
./build.sh

echo ""
echo "============================="
echo "Running Kvasir Unit Tests..."
echo "============================="
./build_release/tests/kvasir_tests "$@"
