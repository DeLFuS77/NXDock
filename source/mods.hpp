// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <string>
#include <vector>

namespace nxdock {

// Cheats and mods (10A). Cheats come from the library app (it keeps switch-cheats-db) for the
// version installed, and go to atmosphere/contents/<title ID>/cheats/<build ID>.txt. Mods are
// .zip packages on the SD card; what NXDock writes of each is written down, and removing takes
// away only that.

// Where a file of a mod package goes inside atmosphere/contents/<title ID>/ ("romfs/…",
// "exefs/…", "cheats/…"), or empty when it is not for this game. Packages come as
// atmosphere/contents/<id>/…, contents/<id>/…, <id>/…, a folder with romfs/exefs inside, or
// romfs/exefs at the top.
std::string ModTarget(const std::string& entry, unsigned long long title_id);

// The packages of a game: sdmc:/switch/NXDock/mods/<title ID>/*.zip, and any zip in mods/ that
// has files for it.
struct ModPackage {
    std::string name, path;
    int files = 0;
    unsigned long long bytes = 0;
    bool on = false; // its files are in place (its list is there)
};
std::vector<ModPackage> ModPackages(const std::string& mods_dir, const std::string& state_dir, unsigned long long title_id);

// Puts the files of a package in place under `contents_dir` (atmosphere/contents/<id>), and
// writes down which. Fails without writing when a file is already there and is not one of its
// own (another mod); `conflict` says which.
bool InstallMod(const ModPackage& pkg, unsigned long long title_id, const std::string& contents_dir, const std::string& state_dir,
                std::string& conflict);
// Takes away what the package put, and the folders left empty.
bool RemoveMod(const ModPackage& pkg, unsigned long long title_id, const std::string& contents_dir, const std::string& state_dir);

// What stops working when a game goes from one build to another (19A): its cheat files when none
// is for the new build, IPS patches of exefs_patches made for the old build, and the exefs files
// of its mods (made for one version). `atmosphere` is sdmc:/atmosphere (a folder on the
// computer in the tests). Builds are 16 (or more) hexadecimal digits; an empty new build tells
// nothing, an empty old one counts what is not for the new.
struct ModsAtRisk {
    int cheats = 0;     // cheat files, none of them for the new build
    int patches = 0;    // exefs_patches/*/<old build>.ips
    bool exefs = false; // contents/<id>/exefs has files
    bool Any() const { return cheats || patches || exefs; }
};
ModsAtRisk ModsAtRiskFor(const std::string& atmosphere, unsigned long long title_id, const std::string& old_build,
                         const std::string& new_build);

// sdmc:/switch/NXDock/mods and its state folder, and sdmc:/atmosphere/contents/<id>.
std::string ModsDir();
std::string ModsStateDir();
std::string ContentsDir(unsigned long long title_id);

} // namespace nxdock
