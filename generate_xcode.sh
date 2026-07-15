#!/bin/bash
# Configure-only CMake run with the Xcode generator into build-mac/, so the
# project can be opened and browsed in Xcode without waiting for a build.
# Reuses build_and_run_mac.sh's build dir/generator (see build-scripts.md).
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build-mac -G Xcode
