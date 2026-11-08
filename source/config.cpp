// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "config.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace nxdock {
namespace {

constexpr const char* kDir = "sdmc:/config/NXDock";
constexpr const char* kPath = "sdmc:/config/NXDock/config.ini";

std::string Trim(const char* s) {
    std::string out(s);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

} // namespace

Config Config::Load() {
    Config c;
    FILE* f = std::fopen(kPath, "r");
    if (!f) {
        return c;
    }
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        if (!std::strncmp(line, "url=", 4)) c.url = Trim(line + 4);
        else if (!std::strncmp(line, "key=", 4)) c.key = Trim(line + 4);
        else if (!std::strncmp(line, "theme=", 6)) c.light = Trim(line + 6) == "light";
    }
    std::fclose(f);
    return c;
}

bool Config::Save() const {
    mkdir("sdmc:/config", 0777);
    mkdir(kDir, 0777);
    FILE* f = std::fopen(kPath, "w");
    if (!f) {
        return false;
    }
    std::fprintf(f, "url=%s\nkey=%s\ntheme=%s\n", url.c_str(), key.c_str(), light ? "light" : "dark");
    std::fclose(f);
    return true;
}

} // namespace nxdock
