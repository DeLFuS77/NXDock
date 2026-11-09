#!/bin/sh
# Builds and runs the tests that need no console, with the g++ of the devkitPro container.
# Run tools/build.sh once first: the catalog test uses the yyjson it fetches.
set -e
cd "$(dirname "$0")/.."
src="$(pwd -W 2>/dev/null || pwd)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$src:/src" -w /src devkitpro/devkita64 sh -c '
  set -e
  g++ -std=c++20 -Wall -Wextra -O1 -o /tmp/json_test tests/json_test.cpp source/json.cpp && /tmp/json_test
  gcc -O1 -c -o /tmp/yyjson.o build/_deps/yyjson-src/src/yyjson.c
  g++ -std=c++20 -Wall -Wextra -O1 -Ibuild/_deps/yyjson-src/src -o /tmp/catalog_test tests/catalog_test.cpp source/catalog.cpp /tmp/yyjson.o && /tmp/catalog_test'
