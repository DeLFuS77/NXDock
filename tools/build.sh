#!/bin/sh
# Builds NXDock.nro inside the official devkitPro container.
# Usage: tools/build.sh   (from anywhere in the repository; needs Docker)
set -e
cd "$(dirname "$0")/.."
src="$(pwd -W 2>/dev/null || pwd)"   # pwd -W gives a Windows path under Git Bash
MSYS_NO_PATHCONV=1 docker run --rm -v "$src:/src" -w /src devkitpro/devkita64 sh -c '
  /opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake -S . -B build -DCMAKE_BUILD_TYPE=Release &&
  cmake --build build -j"$(nproc)"'
echo "Built: build/NXDock.nro"
