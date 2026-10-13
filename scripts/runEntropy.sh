#!/bin/bash
# ============================================================================
# KAIROS - Interactive Entropy Module Runner
# ============================================================================

cd "$(dirname "$0")/.."

echo "====================================================================="
echo "  Starting KAIROS Entropy Module Test Bench..."
echo "====================================================================="

# Check executable
if [ -f "./build/bin/EntropyCLI" ]; then
    CLI_EXEC="./build/bin/EntropyCLI"
elif [ -f "./build/bin/EntropyCLI.exe" ]; then
    CLI_EXEC="./build/bin/EntropyCLI.exe"
else
    echo "[Info] EntropyCLI not found. Building target..."
    cmake --build build --target EntropyCLI --config Release
    if [ -f "./build/bin/EntropyCLI" ]; then
        CLI_EXEC="./build/bin/EntropyCLI"
    else
        CLI_EXEC="./build/bin/EntropyCLI.exe"
    fi
fi

$CLI_EXEC 127.0.0.1 8080
