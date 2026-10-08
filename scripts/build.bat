@echo off
setlocal
where python3 >nul 2>nul
if errorlevel 1 (
    echo ERROR: python3 was not found. Install Python 3 and add it to PATH.
    pause
    exit /b 9009
)
set "preset=%~1"
if not defined preset set "preset=debug"
pushd "%~dp0.."
python3 scripts\ci.py build-only --preset "%preset%"
set "result=%errorlevel%"
popd
echo.
echo Build script finished with exit code %result%.
pause
exit /b %result%
