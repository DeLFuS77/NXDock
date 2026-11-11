// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The list of the games NXDock compressed, and the look for sigpatches on an SD card made up here
// (24A).
#include "compress_meta.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void Touch(const std::string& p, const char* text = "") {
    if (FILE* f = std::fopen(p.c_str(), "w")) {
        std::fputs(text, f);
        std::fclose(f);
    }
}

int main() {
    const std::string root = "/tmp/compress_meta";
    std::system(("rm -rf " + root + " && mkdir -p " + root).c_str());
    {
        const std::string path = root + "/compressed.txt";
        CHECK(LoadCompressed(path).empty());
        std::vector<CompressedGame> games = {{0x0100BB801B79C000ull, "0123456789ABCDEF0123456789ABCDEF", 3900000000ull, 1794300000},
                                             {0x01003DA0254A4000ull, "FEDCBA9876543210FEDCBA9876543210", 20000000ull, 1794300100}};
        CHECK(SaveCompressed(path, games));
        const auto back = LoadCompressed(path);
        CHECK(back.size() == 2 && back[0].app_id == games[0].app_id && back[0].program == games[0].program && back[0].saved == games[0].saved &&
              back[1].when == games[1].when);
        CHECK(SaveCompressed(path, {}) && LoadCompressed(path).empty());
    }
    {
        // no patches, then each of the three ways
        const std::string sd = root + "/sd";
        std::system(("mkdir -p " + sd + "/atmosphere/kip_patches/fs_patches/empty " + sd + "/bootloader").c_str());
        CHECK(!FindSigpatches(sd).found); // an empty folder is not patches
        Touch(sd + "/bootloader/patches.ini", "[Loader:1]\n.nosigchk=0:0x0:0x1,00\n");
        CHECK(!FindSigpatches(sd).found);
        Touch(sd + "/bootloader/patches.ini", "[FS:0123456789ABCDEF]\n.nosigchk=0:0x0:0x1,00\n");
        auto s = FindSigpatches(sd);
        CHECK(s.found && s.how == "bootloader/patches.ini");
        Touch(sd + "/atmosphere/kip_patches/fs_patches/empty/ABCDEF.ips");
        s = FindSigpatches(sd);
        CHECK(s.found && s.how == "atmosphere/kip_patches/fs_patches");
        std::system(("rm -rf " + sd + " && mkdir -p " + sd + "/atmosphere/contents/420000000000000B").c_str());
        Touch(sd + "/atmosphere/contents/420000000000000B/exefs.nsp");
        s = FindSigpatches(sd);
        CHECK(s.found && s.how == "sys-patch");
    }
    if (failures) std::printf("%d failed\n", failures);
    else std::printf("compress_meta: all passed\n");
    return failures ? 1 : 0;
}
