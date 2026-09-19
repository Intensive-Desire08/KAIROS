#!/bin/bash
# Build script for PseudoQuantum Entropy Service

# Ensure we're in the project root
cd "$(dirname "$0")/.."

echo "Configuring CMake project..."
cmake --preset=default

echo "Building project in Release mode..."
cmake --build build --config Release

echo "Build complete."
