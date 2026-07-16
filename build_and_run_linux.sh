#!/usr/bin/env bash
# Current - build the Linux VST3 + Standalone and launch the Standalone.
#
# Linux counterpart of build_and_run_mac.sh. Desktop Linux gets VST3 +
# Standalone only (no AU / AUv3). Installs the VST3 into the user plugin folder
# so a running DAW picks it up, then launches the Standalone.
#
# Headless CI / container use: pass --no-run to build without launching (there's
# no X display to open a window on). The build itself needs no display.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-linux"

RUN_APP=1
for arg in "$@"; do
    case "$arg" in
        --no-run) RUN_APP=0 ;;
    esac
done

# Configure once; reuse the cache on subsequent builds. Ninja if available,
# otherwise the default Make generator.
if [ ! -d "$BUILD_DIR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
    else
        cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release
    fi
fi

cmake --build "$BUILD_DIR" --config Release --parallel

STANDALONE="$BUILD_DIR/Current_artefacts/Release/Standalone/Current"
VST3="$BUILD_DIR/Current_artefacts/Release/VST3/Current.vst3"

# Install the VST3 into the user plugin folder so a running DAW sees it.
VST3_DIR="$HOME/.vst3"
if [ -d "$VST3" ]; then
    mkdir -p "$VST3_DIR"
    rm -rf "$VST3_DIR/Current.vst3"
    cp -R "$VST3" "$VST3_DIR/Current.vst3"
    echo "Installed VST3 to $VST3_DIR/Current.vst3"
fi

if [ "$RUN_APP" -eq 0 ]; then
    echo "Build complete (--no-run: not launching the Standalone)."
    exit 0
fi

if [ ! -x "$STANDALONE" ]; then
    echo "Standalone not found at: $STANDALONE"
    exit 1
fi

echo "Launching Current..."
"$STANDALONE" &
