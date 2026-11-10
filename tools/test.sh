#!/bin/sh
# Builds and runs the tests that need no console, with the g++ of the devkitPro container.
# Run tools/build.sh once first: the catalog test uses the yyjson it fetches.
set -e
cd "$(dirname "$0")/.."
src="$(pwd -W 2>/dev/null || pwd)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$src:/src" -w /src devkitpro/devkita64 sh -c '
  set -e
  g++ -std=c++20 -Wall -Wextra -O1 -o /tmp/json_test tests/json_test.cpp source/json.cpp && /tmp/json_test
  # installing without a server: split files and the USB protocol of Tinfoil
  g++ -std=c++23 -Wall -Wextra -O1 -Itests/shim -Isource -Iinclude -Icompat -o /tmp/local_test tests/local_test.cpp source/file_source.cpp source/usb_tinfoil.cpp source/space_plan.cpp
  /tmp/local_test /tmp
  # the vault of saves: zips written and read, and the list of the library app
  mkdir -p /tmp/zinc && cp /opt/devkitpro/portlibs/switch/include/zlib.h /opt/devkitpro/portlibs/switch/include/zconf.h /tmp/zinc/
  gcc -O1 -c -o /tmp/yyjson_v.o build/_deps/yyjson-src/src/yyjson.c
  g++ -std=c++20 -Wall -Wextra -O1 -Isource -I/tmp/zinc -Ibuild/_deps/yyjson-src/src -o /tmp/vault_test tests/vault_test.cpp source/zip.cpp source/vault_json.cpp /tmp/yyjson_v.o /usr/lib/x86_64-linux-gnu/libz.so.1
  /tmp/vault_test /tmp
  # shops of the kind of Tinfoil and Ownfoil: plain, zstd, zlib, encrypted
  cp /opt/devkitpro/portlibs/switch/include/zstd.h /opt/devkitpro/portlibs/switch/include/zstd_errors.h /tmp/zinc/
  g++ -std=c++20 -Wall -Wextra -O1 -Isource -I/tmp/zinc -Ibuild/_deps/yyjson-src/src -o /tmp/shop_test tests/shop_test.cpp source/shop.cpp /tmp/yyjson_v.o /usr/lib/x86_64-linux-gnu/libz.so.1 /usr/lib/x86_64-linux-gnu/libzstd.so.1
  /tmp/shop_test
  # mod packages put and taken away
  g++ -std=c++20 -Wall -Wextra -O1 -Isource -I/tmp/zinc -o /tmp/mods_test tests/mods_test.cpp source/mods.cpp source/zip.cpp /usr/lib/x86_64-linux-gnu/libz.so.1
  /tmp/mods_test
  # the MTP responder of NXDock, with a fake cable
  g++ -std=c++20 -Wall -Wextra -O1 -Isource -o /tmp/mtp_test tests/mtp_test.cpp source/mtp_proto.cpp source/mtp_storage.cpp
  /tmp/mtp_test /tmp
  # the check of the SD card, against fake cards made up here
  g++ -std=c++20 -Wall -Wextra -O2 -Isource -o /tmp/sdcheck_test tests/sdcheck_test.cpp source/sdcheck.cpp
  /tmp/sdcheck_test
  # the journal of an install cut by the console going off, cut at every byte and every step
  g++ -std=c++20 -Wall -Wextra -O1 -Isource -o /tmp/resume_test tests/resume_test.cpp source/resume.cpp
  /tmp/resume_test /tmp
  # clear errors and the check before installing, in every language
  g++ -std=c++23 -Wall -Wextra -O1 -Itests/shim -Isource -Iinclude -Icompat -o /tmp/errors_test tests/errors_test.cpp source/errors.cpp source/i18n_more.cpp source/precheck.cpp
  /tmp/errors_test
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
  # HTTPS with a pinned key: certificates made here, the pin checked against openssl
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
  # what the web asks of the queue
  g++ -std=c++20 -Wall -Wextra -O1 -Isource -I/tmp/inc -Ibuild/_deps/yyjson-src/src -DNXDOCK_VERSION=\"test\" -o /tmp/queue_report_test tests/queue_report_test.cpp source/queue_report.cpp source/http.cpp source/tls_pin.cpp /tmp/yyjson.o /usr/lib/x86_64-linux-gnu/libcurl.so.4 -lpthread
  /tmp/queue_report_test
  # the network source, against a Range server that cuts answers on purpose
  g++ -std=c++23 -Wall -Wextra -O1 -Itests/shim -Isource -Iinclude -Icompat -I/tmp/inc -DNXDOCK_VERSION=\"test\" -o /tmp/http_source_test tests/http_source_test.cpp source/http_source.cpp source/http.cpp source/tls_pin.cpp /usr/lib/x86_64-linux-gnu/libcurl.so.4 -lpthread
  size=12595257
  python3 tests/range_server.py 18765 $size testkey &
  server=$!
  for i in $(seq 1 50); do curl -s -o /dev/null http://127.0.0.1:18765/stats && break; sleep 0.2; done
  /tmp/http_source_test http://127.0.0.1:18765 $size; rc=$?
  kill $server
  exit $rc'
