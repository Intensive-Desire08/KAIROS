@echo off
REM ============================================================================
REM KAIROS - Interactive ML Threat Detection Runner
REM ============================================================================

cd /d "%~dp0\.."

echo =====================================================================
echo   Starting KAIROS ML Threat Detection Test Bench...
echo =====================================================================

IF EXIST "python\.venv\Scripts\python.exe" (
    SET PYTHON_EXEC=python\.venv\Scripts\python.exe
) ELSE (
    SET PYTHON_EXEC=python
)

%PYTHON_EXEC% python\ml_menu.py
if errorlevel 1 (
    pause
)
