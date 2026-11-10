// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// What "Compress games" (24A) keeps and checks besides the compression itself. Plain C++, tested
// on the computer.
#include <cstdint>
#include <string>
#include <vector>

namespace nxdock {

// The games NXDock compressed: sdmc:/switch/NXDock/compressed.txt, one per line. The program
// content of each is kept: when the one installed is another (the game was installed again, or
// repaired), the mark goes by itself.
struct CompressedGame {
    unsigned long long app_id = 0;
    std::string program;          // the content id NXDock wrote (32 hexadecimal digits)
    unsigned long long saved = 0; // bytes it saved
    long long when = 0;           // seconds since 1970
};
std::vector<CompressedGame> LoadCompressed(const std::string& path);
bool SaveCompressed(const std::string& path, const std::vector<CompressedGame>& games);

// Whether the console can start a game whose NCA header is no longer signed by Nintendo: the
// patches of the file system ("sigpatches"). NXDock never installs them; it only looks for them.
//  - atmosphere/kip_patches/fs_patches with .ips files (Atmosphère);
//  - sys-patch, the system module that patches as the console runs (contents/420000000000000B);
//  - FS patches in bootloader/patches.ini (Hekate's fss0).
struct Sigpatches {
    bool found = false;
    std::string how; // which of them, for the screen
};
// `sd` is the root of the SD card ("sdmc:" on the console, a folder on the computer).
Sigpatches FindSigpatches(const std::string& sd);

} // namespace nxdock
