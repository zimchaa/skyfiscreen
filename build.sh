#!/usr/bin/env bash
# Configure (first run) and build the SkyFi Screen firmware.
# Produces build/skyfiscreen.uf2
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR=build

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  echo ">> Configuring (first run)…"
  cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
fi

echo ">> Building…"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo
echo ">> Done. Firmware: $BUILD_DIR/skyfiscreen.uf2"
ls -lh "$BUILD_DIR/skyfiscreen.uf2" 2>/dev/null || true
