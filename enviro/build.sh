#!/usr/bin/env bash
# Build the Enviro hub firmware -> enviro/build/enviro_hub.uf2
set -euo pipefail
cd "$(dirname "$0")"
[ -f build/CMakeCache.txt ] || cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ls -lh build/enviro_hub.uf2
