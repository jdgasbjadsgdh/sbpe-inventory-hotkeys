@echo off
setlocal
set "SBPE_TARGET=%~1"
if not defined SBPE_TARGET set /p "SBPE_TARGET=Paste your SBPE folder path (containing pypy and plugins): "
set "SBPE_TARGET=%SBPE_TARGET:"=%"
if not exist "%SBPE_TARGET%\pypy\pypy3.exe" (
  echo Cannot find bundled pypy. Choose the main Windows SBPE folder.
  pause
  exit /b 1
)
"%SBPE_TARGET%\pypy\pypy3.exe" "%~dp0install.py" "%SBPE_TARGET%"
pause
