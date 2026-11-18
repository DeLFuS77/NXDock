// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
// The name and the icon of NXDock are for its official versions only (TRADEMARKS.md): a program
// built without NXDOCK_OFFICIAL is an unofficial build, and its NACP, made by CMakeLists.txt, has
// the same name as the code. Built once without it and once with -DNXDOCK_OFFICIAL=1.
#include "brand.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace b = nxdock::brand;

static int failed = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        failed++;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: brand_test CMakeLists.txt\n");
        return 2;
    }
#if defined(NXDOCK_OFFICIAL) && NXDOCK_OFFICIAL
    Check(b::kOfficial, "built with NXDOCK_OFFICIAL, an official build");
    Check(std::strcmp(b::kName, "NXDock") == 0, "the name of NXDock");
#else
    Check(!b::kOfficial, "built without NXDOCK_OFFICIAL, an unofficial build");
    Check(std::strcmp(b::kName, "NXDock (unofficial build)") == 0, "the name says it is an unofficial build");
    Check(std::strcmp(b::kShortName, "NXDock") != 0, "the HOME menu shortcut is not called NXDock alone");
#endif
    std::ifstream file(argv[1]);
    std::stringstream cmake;
    cmake << file.rdbuf();
    const std::string text = cmake.str();
    const size_t name = text.find(std::string("set(NXDOCK_NAME \"") + b::kName + "\")");
    Check(name != std::string::npos, "the NACP has the name of the code");
    // the icon set on the line after the name
    const size_t icon = text.find(b::kOfficial ? "/assets/icon.jpg)" : "/assets/unofficial.jpg)", name);
    Check(name != std::string::npos && icon != std::string::npos && icon - name < 120, "the icon goes with the name");
    if (failed) return 1;
    std::printf("brand_test (%s): ok\n", b::kOfficial ? "official" : "unofficial");
    return 0;
}
