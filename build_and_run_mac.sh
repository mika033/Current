#!/bin/bash
# Main dev-loop build for macOS: configure + build (Debug) + install format
# bundles into ~/Library/Audio/Plug-Ins/ (via COPY_PLUGIN_AFTER_BUILD in
# CMakeLists.txt) + launch the Standalone. See
# SnorkelAudioStandards build-scripts.md.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build-mac -G Xcode
cmake --build build-mac --config Debug

open "build-mac/Current_artefacts/Debug/Standalone/Current.app"
