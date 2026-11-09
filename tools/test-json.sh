#!/bin/sh
# Builds and runs the JSON tests on the computer, in the devkitPro container's own g++.
set -e
cd "$(dirname "$0")/.."
src="$(pwd -W 2>/dev/null || pwd)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$src:/src" -w /src devkitpro/devkita64 sh -c '
  g++ -std=c++20 -Wall -Wextra -O1 -o /tmp/json_test tests/json_test.cpp source/json.cpp && /tmp/json_test'
