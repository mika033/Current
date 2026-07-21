#!/usr/bin/env bash
# Current - build the macOS VST3 + Standalone + AU, install the VST3 and AU, and
# launch the Standalone.
#
# macOS counterpart of build_and_run_linux.sh. Current is a MIDI-effect plugin;
# on macOS it ships a VST3 (+ Standalone) plus an AU MIDI-FX .component. The AU
# is the one format Logic Pro / GarageBand accept in their MIDI-FX insert slot
# (it registers as an 'aumi' MIDI processor); the VST3 covers Live, Cubase, FL,
# Reaper, and Bitwig. See CMakeLists.txt for why AU rides the same target here
# rather than a separate one as in Little Arp Monster.
#
# The binaries are Universal (arm64 + x86_64): the arch and the 10.15 deployment
# target are pinned in CMakeLists.txt, so a plain cmake configure already yields
# binaries that load in Intel and Apple-Silicon hosts alike — no Xcode-project
# step (LAM's generate_xcode.sh) is needed for a dev build.
set -e

if [[ "$(uname)" != Darwin ]]; then
    echo "build_and_run_mac.sh is for macOS. On Linux use ./build_and_run_linux.sh." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-mac"

# Configure once; reuse the cache on subsequent builds. Ninja if available,
# otherwise the default (Makefiles) generator. First configure fetches JUCE via
# FetchContent, so it takes a few minutes; later builds are incremental.
if [ ! -d "$BUILD_DIR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
    else
        cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release
    fi
fi

APP="$BUILD_DIR/Current_artefacts/Release/Standalone/Current.app"
VST3="$BUILD_DIR/Current_artefacts/Release/VST3/Current.vst3"
COMPONENT="$BUILD_DIR/Current_artefacts/Release/AU/Current.component"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
VST3_DEST="$VST3_DIR/Current.vst3"

COMPONENT_DIR="$HOME/Library/Audio/Plug-Ins/Components"
COMPONENT_DEST="$COMPONENT_DIR/Current.component"

# Delete the old .app first so the fresh build can't inherit stale codesign /
# provenance state from a previous run.
if [ -d "$APP" ]; then
    xattr -r -d com.apple.provenance "$APP" 2>/dev/null || true
    rm -rf "$APP"
fi

cmake --build "$BUILD_DIR" --config Release --parallel

# Close any running Standalone before reinstalling — otherwise the copy can race
# a mapped-in bundle, and the new build wouldn't be the one running anyway.
if pgrep -x "Current" > /dev/null; then
    pkill -x "Current"
    sleep 1
fi

# Install the VST3 into the user plugin folder so a running DAW picks it up.
# JUCE ad-hoc-signs the bundle during the build.
if [ -d "$VST3" ]; then
    mkdir -p "$VST3_DIR"
    rm -rf "$VST3_DEST"
    cp -R "$VST3" "$VST3_DEST"
    echo "Installed VST3 to $VST3_DEST"
fi

# Install the AU MIDI-FX component, then force the system AU registrar to rescan
# so Logic / GarageBand and auval see the fresh build immediately (registration
# otherwise lags a launch behind).
if [ -d "$COMPONENT" ]; then
    mkdir -p "$COMPONENT_DIR"
    rm -rf "$COMPONENT_DEST"
    cp -R "$COMPONENT" "$COMPONENT_DEST"
    echo "Installed AU to $COMPONENT_DEST"

    killall -9 AudioComponentRegistrar 2>/dev/null || true

    # auval is a diagnostic only — first-registration timing can make it
    # transiently fail, so never let it abort the launch. Type/subtype/manuf are
    # 'aumi' (IS_MIDI_EFFECT) / the plugin code 'Curr' / the manufacturer 'Snrk'.
    echo "Validating AU (auval -v aumi Curr Snrk)..."
    if auval -v aumi Curr Snrk; then
        echo "auval: PASS"
    else
        echo "auval: did not pass (often just first-scan timing — re-run 'auval -v aumi Curr Snrk')."
    fi
else
    echo "WARNING: AU component not found at $COMPONENT"
fi

echo "(Restart your DAW if it had a previous build loaded.)"

if [ ! -d "$APP" ]; then
    echo "Standalone not found at: $APP"
    exit 1
fi

echo "Launching Current..."
open "$APP"
