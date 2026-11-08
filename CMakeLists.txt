cmake_minimum_required(VERSION 3.13)

project(NXDock VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(nxdock
    source/main.cpp
)

target_compile_definitions(nxdock PRIVATE
    NXDOCK_VERSION="${PROJECT_VERSION}"
)

target_compile_options(nxdock PRIVATE -Wall -Wextra -O2)

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
