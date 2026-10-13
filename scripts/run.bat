@echo off
REM Run script for KAIROS Entropy Module (Windows)

REM Ensure we're in the project root
cd /d "%~dp0\.."

SET CONFIG_FILE=%~1
IF "%CONFIG_FILE%"=="" SET CONFIG_FILE=config\backend_config.json

IF EXIST "build\bin\EntropyModule.exe" (
    echo Running KAIROS Entropy Module with config: %CONFIG_FILE%
    "build\bin\EntropyModule.exe" "%CONFIG_FILE%"
) ELSE IF EXIST "build\bin\Release\EntropyModule.exe" (
    echo Running KAIROS Entropy Module with config: %CONFIG_FILE%
    "build\bin\Release\EntropyModule.exe" "%CONFIG_FILE%"
) ELSE (
    echo Error: EntropyModule.exe not found. Please build the project first.
    exit /b 1
)
