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
  # the sounds, with the synth the console runs
  g++ -std=c++20 -Wall -Wextra -O2 -Isource -o /tmp/sound_test tests/sound_test.cpp source/synth.cpp && /tmp/sound_test
  # game card images, which container a file is, and the install history
  g++ -std=c++23 -Wall -Wextra -O1 -Itests/shim -Isource -Iinclude -Icompat -Wno-missing-field-initializers -o /tmp/xci_history_test tests/xci_history_test.cpp source/yati/container/xci.cpp source/history.cpp && /tmp/xci_history_test /tmp
  # the LZ4 compressors of Nintendo against the blocks the library app makes, and NCZSECT2/3
  # against the NCA it gives of them; zstd and curl headers from the portlibs, the libraries of
  # the container
  mkdir -p /tmp/inc && cp -r /opt/devkitpro/portlibs/switch/include/curl /opt/devkitpro/portlibs/switch/include/zstd.h /opt/devkitpro/portlibs/switch/include/zstd_errors.h /tmp/inc/
  gcc -std=c11 -Wall -Wextra -O2 -Iinclude -c -o /tmp/lz4nintendo.o source/lz4nintendo.c
  g++ -std=c++20 -Wall -Wextra -O2 -Iinclude -o /tmp/lz4nintendo_test tests/lz4nintendo_test.cpp /tmp/lz4nintendo.o
  /tmp/lz4nintendo_test tests/fixtures/lz4nintendo_golden.txt LICENSE assets/icon.jpg
  g++ -std=c++23 -Wall -Wextra -O2 -Itests/shim -Iinclude -Icompat -I/tmp/inc -o /tmp/nczsect_test tests/nczsect_test.cpp source/yati/nx/nczsect.cpp /tmp/lz4nintendo.o /usr/lib/x86_64-linux-gnu/libzstd.so.1
  /tmp/nczsect_test tests/fixtures
  # the network source, against a Range server that cuts answers on purpose
  g++ -std=c++23 -Wall -Wextra -O1 -Itests/shim -Isource -Iinclude -Icompat -I/tmp/inc -DNXDOCK_VERSION=\"test\" -o /tmp/http_source_test tests/http_source_test.cpp source/http_source.cpp /usr/lib/x86_64-linux-gnu/libcurl.so.4 -lpthread
  size=12595257
  python3 tests/range_server.py 18765 $size testkey &
  server=$!
  for i in $(seq 1 50); do curl -s -o /dev/null http://127.0.0.1:18765/stats && break; sleep 0.2; done
  /tmp/http_source_test http://127.0.0.1:18765 $size; rc=$?
  kill $server
  exit $rc'
