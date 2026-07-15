#!/bin/bash
# Configure-only CMake run into a dedicated build-clangd/ side dir so
# clangd can resolve JUCE includes. See clangd-setup.md. Re-run after
# adding/removing files in CMakeLists.txt's target_sources.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build-clangd -G Ninja \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15

ln -sf build-clangd/compile_commands.json compile_commands.json
