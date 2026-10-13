#!/bin/bash
# Run script for KAIROS Entropy Module

# Ensure we're in the project root
cd "$(dirname "$0")/.."

CONFIG_FILE=${1:-"config/backend_config.json"}

# Determine executable name based on build output
if [ -f "./build/bin/EntropyModule.exe" ]; then
    EXECUTABLE="./build/bin/EntropyModule.exe"
elif [ -f "./build/bin/EntropyModule" ]; then
    EXECUTABLE="./build/bin/EntropyModule"
elif [ -f "./build/bin/Release/EntropyModule.exe" ]; then
    EXECUTABLE="./build/bin/Release/EntropyModule.exe"
elif [ -f "./build/bin/Release/EntropyModule" ]; then
    EXECUTABLE="./build/bin/Release/EntropyModule"
else
    echo "Error: EntropyModule executable not found. Please build the project first."
    exit 1
fi

echo "Running KAIROS Entropy Module with config: $CONFIG_FILE"
$EXECUTABLE "$CONFIG_FILE"
