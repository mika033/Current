#!/bin/bash
# Relaunch the already-built Standalone. No rebuild, no install.
set -euo pipefail
cd "$(dirname "$0")"

open "build-mac/Current_artefacts/Debug/Standalone/Current.app"
