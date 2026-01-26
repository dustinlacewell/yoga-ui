#!/bin/bash
# Build and run yui tests

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YUI_DIR="$(dirname "$SCRIPT_DIR")"

cd "$YUI_DIR"

# Clean and rebuild
rm -rf build
mkdir -p build
cd build

cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug || exit 1
make -j$(nproc) test_runner || exit 1

echo ""
echo "Running tests..."
./bin/test_runner.exe "$@"
