#!/bin/sh
# Builds NXDock.nro inside the official devkitPro container.
# Usage: tools/build.sh   (from the repository root; needs Docker)
set -e
cd "$(dirname "$0")/.."
docker run --rm -v "$(pwd):/src" -w /src devkitpro/devkita64 sh -c '
  /opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake -S . -B build -DCMAKE_BUILD_TYPE=Release &&
  cmake --build build -j"$(nproc)"'
echo "Built: build/NXDock.nro"
