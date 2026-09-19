#!/bin/bash
# Run script for PseudoQuantum Entropy Service

# Ensure we're in the project root
cd "$(dirname "$0")/.."

CONFIG_FILE=${1:-"config/backend_config.json"}

# Determine executable name based on OS output
if [ -f "./build/bin/Release/PseudoQuantumEntropy.exe" ]; then
    EXECUTABLE="./build/bin/Release/PseudoQuantumEntropy.exe"
elif [ -f "./build/bin/Release/PseudoQuantumEntropy" ]; then
    EXECUTABLE="./build/bin/Release/PseudoQuantumEntropy"
else
    echo "Error: Executable not found. Please build the project first."
    exit 1
fi

echo "Running with config: $CONFIG_FILE"
$EXECUTABLE "$CONFIG_FILE"
