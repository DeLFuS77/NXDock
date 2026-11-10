cmake_minimum_required(VERSION 3.13)

project(NXDock VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# nanovg for deko3d (zlib licence), the same revision Sphaira uses.
set(NANOVG_NO_PSD ON)
set(NANOVG_NO_TGA ON)
set(NANOVG_NO_GIF ON)
set(NANOVG_NO_HDR ON)
set(NANOVG_NO_PIC ON)
set(NANOVG_NO_PNM ON)
FetchContent_Declare(nanovg
    GIT_REPOSITORY https://github.com/ITotalJustice/nanovg-deko3d.git
    GIT_TAG 2acb0f3
)
FetchContent_MakeAvailable(nanovg)

# yyjson (MIT licence), the JSON reader, the same version Sphaira uses.
set(YYJSON_INSTALL OFF)
set(YYJSON_DISABLE_WRITER ON)
set(YYJSON_DISABLE_UTILS ON)
set(YYJSON_DISABLE_NON_STANDARD ON)
FetchContent_Declare(yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 0.12.0
)
FetchContent_MakeAvailable(yyjson)

# QR Code generator by Project Nayuki (MIT licence), the C version: the QR of the pairing screen.
# Its folder has no CMake project, so it is only fetched and its one file built here.
FetchContent_Declare(qrcodegen
    GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
    GIT_TAG v1.8.0
    SOURCE_SUBDIR c
)
FetchContent_MakeAvailable(qrcodegen)
add_library(qrcodegen STATIC ${qrcodegen_SOURCE_DIR}/c/qrcodegen.c)
target_include_directories(qrcodegen PUBLIC ${qrcodegen_SOURCE_DIR}/c)
target_compile_options(qrcodegen PRIVATE -O2)

# USB disks (FAT32 and exFAT) with libusbhsfs (ISC licence), the revision Sphaira uses; NTFS
# and EXT4 (GPL) are left out.
set(USBHSFS_GPL OFF)
set(USBHSFS_NTFS OFF)
set(USBHSFS_EXT4 OFF)
set(USBHSFS_SXOS_DISABLE ON)
FetchContent_Declare(libusbhsfs
    GIT_REPOSITORY https://github.com/ITotalJustice/libusbhsfs.git
    GIT_TAG 625269b
)
FetchContent_MakeAvailable(libusbhsfs)

# The install core "yati" from Sphaira (GPL-3.0), with NXDock's layer in place of Sphaira's
# app, settings, translations and log (compat/). See NOTICE.
add_library(yati STATIC
    source/yati/yati.cpp
    source/yati/container/nsp.cpp
    source/yati/container/xci.cpp
    source/yati/nx/es.cpp
    source/yati/nx/keys.cpp
    source/yati/nx/nca.cpp
    source/yati/nx/ncm.cpp
    source/yati/nx/ns.cpp
    source/yati/nx/nczsect.cpp
    source/lz4nintendo.c
    source/yati/nx/nxdumptool_rsa.c
    source/yati/nx/nxdumptool/save.c
    compat/yati_env.cpp
)
target_include_directories(yati PUBLIC include compat)
target_include_directories(yati PRIVATE include/yati/nx/nxdumptool)
target_compile_definitions(yati PRIVATE NXDOCK_VERSION="${PROJECT_VERSION}")
# the core keeps its upstream style: signed/unsigned comparisons and partial initialisers are
# not reported there; NXDock's own code is built with every -Wall -Wextra warning.
target_compile_options(yati PRIVATE -Wall -Wextra -Wno-sign-compare -Wno-missing-field-initializers -O2)
# NXDock's own files in the core get every warning back.
set_source_files_properties(source/yati/nx/nczsect.cpp source/lz4nintendo.c
    PROPERTIES COMPILE_OPTIONS "-Wsign-compare;-Wmissing-field-initializers")
find_path(zstd_inc zstd.h REQUIRED)
find_library(zstd_lib zstd REQUIRED)
find_library(mbedcrypto_lib mbedcrypto REQUIRED)
target_include_directories(yati PRIVATE ${zstd_inc})
target_link_libraries(yati PUBLIC ${zstd_lib} ${mbedcrypto_lib})

add_executable(nxdock
    source/main.cpp
    source/app.cpp
    source/catalog.cpp
    source/config.cpp
    source/covers.cpp
    source/discovery.cpp
    source/http.cpp
    source/history.cpp
    source/http_source.cpp
    source/install_queue.cpp
    source/json.cpp
    source/link.cpp
    source/net.cpp
    source/push.cpp
    source/safety.cpp
    source/sound.cpp
    source/speedtest.cpp
    source/synth.cpp
    source/tls_pin.cpp
    source/uninstall.cpp
    source/verify.cpp
    source/wizard.cpp
    source/gfx.cpp
    source/i18n.cpp
    source/i18n_more.cpp
    source/file_source.cpp
    source/motion.cpp
    source/more.cpp
    source/more_vault.cpp
    source/more_space.cpp
    source/more_shop.cpp
    source/more_mods.cpp
    source/screen_off.cpp
    source/mods.cpp
    source/shop.cpp
    source/shop_load.cpp
    source/space.cpp
    source/sdcheck.cpp
    source/queue_report.cpp
    source/resume.cpp
    source/romfs_lz4.cpp
    source/patch_view.cpp
    source/compress_meta.cpp
    source/cnmt_patch.cpp
    source/compress_nx.cpp
    source/sdcheck_nx.cpp
    source/more_sdcheck.cpp
    source/more_compress.cpp
    source/space_plan.cpp
    source/vault.cpp
    source/vault_json.cpp
    source/zip.cpp
    source/mtp.cpp
    source/mtp_proto.cpp
    source/mtp_storage.cpp
    source/mtp_usb.cpp
    source/usb_disks.cpp
    source/usb_tinfoil.cpp
    source/errors.cpp
    source/precheck.cpp
    source/system_info.cpp
    source/ui.cpp
)

target_compile_definitions(nxdock PRIVATE
    NXDOCK_VERSION="${PROJECT_VERSION}"
)

target_compile_options(nxdock PRIVATE -Wall -Wextra -O2)

find_library(deko3d_lib deko3d REQUIRED)
# libcurl and zlib from devkitPro's portlibs (switch-curl, switch-zlib)
find_path(curl_inc curl/curl.h REQUIRED)
find_library(curl_lib curl REQUIRED)
find_library(z_lib z REQUIRED)
target_include_directories(nxdock PRIVATE ${curl_inc} ${zstd_inc})
target_link_libraries(nxdock PRIVATE yati nanovg yyjson qrcodegen stb_nanovg libusbhsfs ${deko3d_lib} ${curl_lib} ${z_lib})

nx_generate_nacp(
    OUTPUT nxdock.nacp
    NAME "NXDock"
    AUTHOR "DeLFuS77"
    VERSION "${PROJECT_VERSION}"
)

nx_create_nro(nxdock
    OUTPUT ${CMAKE_BINARY_DIR}/NXDock.nro
    ICON ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.jpg
    NACP nxdock.nacp
)
