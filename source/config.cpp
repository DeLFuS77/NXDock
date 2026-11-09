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
constexpr const char* kTemp = "sdmc:/config/NXDock/config.ini.tmp";

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
        else if (!std::strncmp(line, "id=", 3)) c.id = Trim(line + 3);
        else if (!std::strncmp(line, "name=", 5)) c.name = Trim(line + 5);
        else if (!std::strncmp(line, "theme=", 6)) c.light = Trim(line + 6) == "light";
    }
    std::fclose(f);
    return c;
}

bool Config::Save() const {
    mkdir("sdmc:/config", 0777);
    mkdir(kDir, 0777);
    // written whole to a new file and then put in place: a power cut never leaves half of it
    FILE* f = std::fopen(kTemp, "w");
    if (!f) {
        return false;
    }
    std::fprintf(f, "url=%s\nkey=%s\nid=%s\nname=%s\ntheme=%s\n", url.c_str(), key.c_str(), id.c_str(), name.c_str(),
                 light ? "light" : "dark");
    const bool ok = std::fclose(f) == 0;
    if (!ok) {
        std::remove(kTemp);
        return false;
    }
    std::remove(kPath);
    return std::rename(kTemp, kPath) == 0;
}

} // namespace nxdock
