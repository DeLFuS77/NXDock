// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "compress_meta.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nxdock {
namespace {

bool EndsWithNoCase(const std::string& s, const char* end) {
    const size_t n = std::strlen(end);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        if (std::tolower((unsigned char)s[s.size() - n + i]) != std::tolower((unsigned char)end[i])) return false;
    }
    return true;
}

bool Exists(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

int CountIps(const std::string& dir) {
    int n = 0;
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        if (EndsWithNoCase(name, ".ips")) n++;
        else n += CountIps(dir + "/" + name); // a folder per patch set
    }
    closedir(d);
    return n;
}

} // namespace

std::vector<CompressedGame> LoadCompressed(const std::string& path) {
    std::vector<CompressedGame> out;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return out;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        char program[40] = {};
        unsigned long long app = 0, saved = 0;
        long long when = 0;
        if (std::sscanf(line, "%llx %39s %llu %lld", &app, program, &saved, &when) == 4 && std::strlen(program) == 32) {
            out.push_back({app, program, saved, when});
        }
    }
    std::fclose(f);
    return out;
}

bool SaveCompressed(const std::string& path, const std::vector<CompressedGame>& games) {
    if (games.empty()) {
        std::remove(path.c_str());
        return true;
    }
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return false;
    for (const auto& g : games) std::fprintf(f, "%016llX %s %llu %lld\n", g.app_id, g.program.c_str(), g.saved, g.when);
    if (std::fclose(f) != 0) return false;
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

Sigpatches FindSigpatches(const std::string& sd) {
    Sigpatches s;
    if (CountIps(sd + "/atmosphere/kip_patches/fs_patches") > 0) {
        s.found = true;
        s.how = "atmosphere/kip_patches/fs_patches";
        return s;
    }
    if (Exists(sd + "/atmosphere/contents/420000000000000B/exefs.nsp")) {
        s.found = true;
        s.how = "sys-patch";
        return s;
    }
    if (FILE* f = std::fopen((sd + "/bootloader/patches.ini").c_str(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            if (std::strncmp(line, "[FS:", 4) == 0) {
                s.found = true;
                s.how = "bootloader/patches.ini";
                break;
            }
        }
        std::fclose(f);
    }
    return s;
}

} // namespace nxdock
