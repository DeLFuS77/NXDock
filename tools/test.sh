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
  g++ -std=c++20 -Wall -Wextra -O1 -Ibuild/_deps/yyjson-src/src -o /tmp/catalog_test tests/catalog_test.cpp source/catalog.cpp /tmp/yyjson.o && /tmp/catalog_test
  # the network source, against a Range server that cuts answers on purpose; curl headers from
  # the portlibs, the library of the container
  mkdir -p /tmp/inc && cp -r /opt/devkitpro/portlibs/switch/include/curl /tmp/inc/
  g++ -std=c++23 -Wall -Wextra -O1 -Itests/shim -Isource -Iinclude -Icompat -I/tmp/inc -DNXDOCK_VERSION=\"test\" -o /tmp/http_source_test tests/http_source_test.cpp source/http_source.cpp /usr/lib/x86_64-linux-gnu/libcurl.so.4 -lpthread
  size=12595257
  python3 tests/range_server.py 18765 $size testkey &
  server=$!
  for i in $(seq 1 50); do curl -s -o /dev/null http://127.0.0.1:18765/stats && break; sleep 0.2; done
  /tmp/http_source_test http://127.0.0.1:18765 $size; rc=$?
  kill $server
  exit $rc'
