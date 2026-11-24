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
//  - FS patches in bootloader/patches.ini (Hekate's fss0);
//  - an Atmosphère with the patches inside it, which leaves no file of them (the fork of
//    borntohonk, "Atmosphère+", made for firmware 23.5.0, where the .ips of 23.0 no longer apply).
struct Sigpatches {
    bool found = false;
    std::string how; // which of them, for the screen
};
// `sd` is the root of the SD card ("sdmc:" on the console, a folder on the computer); `embedded`,
// what AtmosphereEmbedsPatches() said.
Sigpatches FindSigpatches(const std::string& sd, bool embedded = false);

// Whether the running Atmosphère patches the signatures by itself (asked of exosphère once, on
// the console; false on the computer).
bool AtmosphereEmbedsPatches();

// The look for sigpatches, kept: the SD card is read the first time they are asked for, and again
// only after Refresh (NXDock back in focus: they may have been put on the SD card meanwhile).
class SigpatchCache {
public:
    const Sigpatches& Get(const std::string& sd);
    void Refresh() { m_read = false; }

private:
    Sigpatches m_found;
    bool m_read = false;
};

// The orange line on the page of a game (5B): its option (sigpatch_notice) on and no sigpatches
// found. Only a warning, never in the way of installing. With the option off, the SD card is not read.
bool SigpatchLine(bool option_on, SigpatchCache& cache, const std::string& sd);

} // namespace nxdock
