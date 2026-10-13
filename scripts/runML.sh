#!/bin/bash
# ============================================================================
# KAIROS - Interactive ML Threat Detection Runner
# ============================================================================

cd "$(dirname "$0")/.."

echo "====================================================================="
echo "  Starting KAIROS ML Threat Detection Test Bench..."
echo "====================================================================="

if [ -f "./python/.venv/bin/python" ]; then
    PYTHON_EXEC="./python/.venv/bin/python"
elif [ -f "./python/.venv/Scripts/python.exe" ]; then
    PYTHON_EXEC="./python/.venv/Scripts/python.exe"
else
    PYTHON_EXEC="python3"
fi

$PYTHON_EXEC python/ml_menu.py
