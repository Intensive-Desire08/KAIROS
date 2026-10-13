@echo off
REM ============================================================================
REM KAIROS - Interactive Entropy Module Runner
REM ============================================================================

cd /d "%~dp0\.."

echo =====================================================================
echo   Starting KAIROS Entropy Module Test Bench...
echo =====================================================================

REM Ensure EntropyCLI executable exists
IF NOT EXIST "build\bin\EntropyCLI.exe" (
    echo [Info] EntropyCLI.exe not found in build\bin\. Building target...
    cmake --build build --target EntropyCLI --config Release
    IF NOT EXIST "build\bin\EntropyCLI.exe" (
        echo [Error] Failed to build EntropyCLI.exe. Please build project first.
        pause
        exit /b 1
    )
)

REM Check if backend daemon EntropyModule.exe exists
IF NOT EXIST "build\bin\EntropyModule.exe" (
    echo [Info] EntropyModule.exe not found. Building target...
    cmake --build build --target EntropyModule --config Release
)

echo [Info] Launching Entropy Module Interactive CLI...
"build\bin\EntropyCLI.exe" 127.0.0.1 8080
