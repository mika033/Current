@echo off
REM Main dev-loop build for Windows. Default is a Debug build; pass
REM "product" for a Release build. See SnorkelAudioStandards
REM build-scripts.md.
setlocal

set CONFIG=Debug
if /I "%~1"=="product" set CONFIG=Release

cd /d "%~dp0"

cmake -B build-win -G "Visual Studio 17 2022"
if errorlevel 1 exit /b 1

cmake --build build-win --config %CONFIG%
if errorlevel 1 exit /b 1

start "" "build-win\Current_artefacts\%CONFIG%\Standalone\Current.exe"
