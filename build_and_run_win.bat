@echo off
REM Current - build the Windows plugin, install the VST3, and launch the
REM Standalone. Windows counterpart of build_and_run_linux.sh /
REM build_and_run_mac.sh.
REM
REM Desktop Windows ships a single VST3 (+ Standalone). There is no AU on Windows
REM (AU is a macOS format), and no "MIDI-effect" VST3: VST3 has no real MIDI-FX
REM insert slot in any DAW, so a normal VST3 whose MIDI output you route to the
REM instrument covers Live, Cubase, FL, Reaper, and Bitwig alike (see
REM CMakeLists.txt). Current's CMake does NOT use COPY_PLUGIN_AFTER_BUILD, so this
REM script installs the VST3 explicitly.
echo === Current Build (Windows) ===

set BUILD_DIR=build-win
set VST3_DEST=%CommonProgramFiles%\VST3
set VST3=%BUILD_DIR%\Current_artefacts\Release\VST3\Current.vst3
set APP=%BUILD_DIR%\Current_artefacts\Release\Standalone\Current.exe

REM Close a running standalone (or a DAW) that has the exe / VST3 locked.
REM Process name comes from PRODUCT_NAME.
taskkill /F /IM "Current.exe" >nul 2>&1

echo Configuring CMake...
cmake -B %BUILD_DIR% -G "Visual Studio 18 2026"
if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed.
    pause
    exit /b 1
)

echo Building...
cmake --build %BUILD_DIR% --config Release --parallel
if %ERRORLEVEL% neq 0 (
    echo Build failed.
    pause
    exit /b 1
)

REM Install the VST3 bundle to the standard Windows VST3 folder. On Windows a
REM .vst3 is a bundle DIRECTORY, so copy recursively (and wipe the old one first
REM to avoid stale files). %CommonProgramFiles%\VST3 is system-wide and usually
REM needs an elevated shell. The install is best-effort: a failed copy only warns
REM (run elevated to install the VST3 for a DAW) and does NOT abort, so the dev
REM loop of build + launch standalone keeps working without Administrator.
echo Installing VST3 to "%VST3_DEST%"...
if not exist "%VST3_DEST%" mkdir "%VST3_DEST%" 2>nul

if exist "%VST3_DEST%\Current.vst3" rmdir /S /Q "%VST3_DEST%\Current.vst3"
xcopy /E /I /Y "%VST3%" "%VST3_DEST%\Current.vst3" >nul
if %ERRORLEVEL% neq 0 (
    echo WARNING: could not install VST3 - run from an elevated shell ^(Administrator^) to install for a DAW.
) else (
    echo Installed: %VST3_DEST%\Current.vst3
)

echo (Restart your DAW if it had a previous build loaded.)

echo === Build successful! Launching standalone... ===
start "" "%APP%"
