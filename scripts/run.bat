@echo off
REM Run script for PseudoQuantum Entropy Service (Windows)

REM Ensure we're in the project root
cd /d "%~dp0\.."

SET CONFIG_FILE=%~1
IF "%CONFIG_FILE%"=="" SET CONFIG_FILE=config\backend_config.json

IF EXIST "build\bin\Release\PseudoQuantumEntropy.exe" (
    echo Running with config: %CONFIG_FILE%
    "build\bin\Release\PseudoQuantumEntropy.exe" "%CONFIG_FILE%"
) ELSE (
    echo Error: Executable not found. Please run build script first.
    exit /b 1
)
