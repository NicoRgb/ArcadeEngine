@echo off
setlocal
where python3 >nul 2>nul
if errorlevel 1 (
    echo ERROR: python3 was not found. Install Python 3 and add it to PATH.
    pause
    exit /b 9009
)
pushd "%~dp0.."
python3 scripts\dev.py %*
set "result=%errorlevel%"
popd
echo.
echo Setup script finished with exit code %result%.
pause
exit /b %result%
