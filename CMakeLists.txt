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

add_executable(nxdock
    source/main.cpp
    source/app.cpp
    source/gfx.cpp
    source/i18n.cpp
    source/ui.cpp
)

target_compile_definitions(nxdock PRIVATE
    NXDOCK_VERSION="${PROJECT_VERSION}"
)

target_compile_options(nxdock PRIVATE -Wall -Wextra -O2)

find_library(deko3d_lib deko3d REQUIRED)
target_link_libraries(nxdock PRIVATE nanovg ${deko3d_lib})

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
