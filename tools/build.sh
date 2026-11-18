#!/bin/sh
# Builds NXDock.nro inside the official devkitPro container.
# Usage: tools/build.sh   (from anywhere in the repository; needs Docker)
#
# The name and the icon of NXDock are for its official versions only (TRADEMARKS.md): the workflow
# of the repository builds them with NXDOCK_OFFICIAL=ON tools/build.sh. Without it NXDock.nro is
# an unofficial build, with a name that says so and a plain icon. It is given to CMake every time,
# so a build folder made one way is not left that way.
set -e
cd "$(dirname "$0")/.."
src="$(pwd -W 2>/dev/null || pwd)"   # pwd -W gives a Windows path under Git Bash
case "${NXDOCK_OFFICIAL:-}" in
  ON|on|1|true) official=ON ;;
  *) official=OFF ;;
esac
MSYS_NO_PATHCONV=1 docker run --rm -v "$src:/src" -w /src -e NXDOCK_OFFICIAL="$official" devkitpro/devkita64 sh -c '
  /opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNXDOCK_OFFICIAL="$NXDOCK_OFFICIAL" &&
  cmake --build build -j"$(nproc)"'
if [ "$official" = ON ]; then
  echo "Built: build/NXDock.nro"
else
  echo "Built: build/NXDock.nro (unofficial build)"
fi
