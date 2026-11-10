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
  # game card images, which container a file is, and the install history
  g++ -std=c++23 -Wall -Wextra -O1 -Itests/shim -Isource -Iinclude -Icompat -Wno-missing-field-initializers -o /tmp/xci_history_test tests/xci_history_test.cpp source/yati/container/xci.cpp source/history.cpp && /tmp/xci_history_test /tmp
  # HTTPS with a pinned key: certificates made here, the pin checked against openssl
  mkdir -p /tmp/inc && cp -r /opt/devkitpro/portlibs/switch/include/curl /tmp/inc/
  mkdir -p /tmp/tls && cd /tmp/tls
  openssl req -x509 -newkey rsa:2048 -nodes -keyout rsa.key -out rsa.pem -days 2 -subj /CN=library 2>/dev/null
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout ec.key -out ec.pem -days 2 -subj /CN=other 2>/dev/null
  cd /src
  g++ -std=c++20 -Wall -Wextra -O1 -Isource -I/tmp/inc -o /tmp/tls_pin_test tests/tls_pin_test.cpp source/tls_pin.cpp /usr/lib/x86_64-linux-gnu/libcurl.so.4
  pin_of() { openssl x509 -in "$1" -pubkey -noout | openssl pkey -pubin -outform der | openssl dgst -sha256 -binary | base64; }
  /tmp/tls_pin_test /tmp/tls/ec.pem "sha256//$(pin_of /tmp/tls/ec.pem)"
  python3 tests/https_server.py 18766 /tmp/tls/rsa.pem /tmp/tls/rsa.key &
  tls_server=$!
  sleep 1
  /tmp/tls_pin_test /tmp/tls/rsa.pem "sha256//$(pin_of /tmp/tls/rsa.pem)" https://127.0.0.1:18766 /tmp/tls/ec.pem
  kill $tls_server
  # the network source, against a Range server that cuts answers on purpose; curl headers from
  # the portlibs, the library of the container
  mkdir -p /tmp/inc && cp -r /opt/devkitpro/portlibs/switch/include/curl /tmp/inc/
  g++ -std=c++23 -Wall -Wextra -O1 -Itests/shim -Isource -Iinclude -Icompat -I/tmp/inc -DNXDOCK_VERSION=\"test\" -o /tmp/http_source_test tests/http_source_test.cpp source/http_source.cpp source/tls_pin.cpp /usr/lib/x86_64-linux-gnu/libcurl.so.4 -lpthread
  size=12595257
  python3 tests/range_server.py 18765 $size testkey &
  server=$!
  for i in $(seq 1 50); do curl -s -o /dev/null http://127.0.0.1:18765/stats && break; sleep 0.2; done
  /tmp/http_source_test http://127.0.0.1:18765 $size; rc=$?
  kill $server
  exit $rc'
