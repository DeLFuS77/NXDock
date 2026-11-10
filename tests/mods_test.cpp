// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Mods on the computer: where each kind of package puts its files, a package put and taken
// away leaving nothing behind, and a second package that would write over the first refused.
#include "mods.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static bool Exists(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

int main() {
    const unsigned long long id = 0x0100000000010000ull;
    CHECK(ModTarget("atmosphere/contents/0100000000010000/romfs/a.bin", id) == "romfs/a.bin");
    CHECK(ModTarget("contents/0100000000010000/exefs/main.npdm", id) == "exefs/main.npdm");
    CHECK(ModTarget("0100000000010000/romfs/x/y.txt", id) == "romfs/x/y.txt");
    CHECK(ModTarget("60fps/exefs/patch.ips", id) == "exefs/patch.ips");
    CHECK(ModTarget("romfs/data.arc", id) == "romfs/data.arc");
    CHECK(ModTarget("RomFS/data.arc", id) == "romfs/data.arc");
    CHECK(ModTarget("contents/0100000000020000/romfs/a.bin", id).empty()); // another game
    CHECK(ModTarget("readme.txt", id).empty());
    CHECK(ModTarget("romfs/../../evil", id).empty());
    CHECK(ModTarget("romfs/", id).empty());

    const std::string root = "/tmp/mods_test";
    std::system(("rm -rf " + root + " && mkdir -p " + root + "/mods/0100000000010000 " + root + "/contents").c_str());
    const std::string py = "python3 -c \"import zipfile; "
                           "z=zipfile.ZipFile('" + root + "/mods/0100000000010000/HD.zip','w',zipfile.ZIP_DEFLATED); "
                           "z.writestr('HD/romfs/tex/a.bin','A'*5000); z.writestr('HD/romfs/tex/b.bin','B'); z.writestr('HD/readme.txt','x'); z.close(); "
                           "z=zipfile.ZipFile('" + root + "/mods/Clash.zip','w'); "
                           "z.writestr('atmosphere/contents/0100000000010000/romfs/tex/a.bin','C'); z.close(); "
                           "z=zipfile.ZipFile('" + root + "/mods/Other.zip','w'); "
                           "z.writestr('contents/0100000000099000/romfs/z.bin','Z'); z.close()\"";
    CHECK(std::system(py.c_str()) == 0);
    const std::string state = root + "/mods/.installed", contents = root + "/contents/0100000000010000";

    auto pkgs = ModPackages(root + "/mods", state, id);
    CHECK(pkgs.size() == 2); // HD and Clash; Other is for another game
    const ModPackage* hd = nullptr;
    const ModPackage* clash = nullptr;
    for (const auto& p : pkgs) {
        if (p.name == "HD") hd = &p;
        if (p.name == "Clash") clash = &p;
    }
    CHECK(hd && hd->files == 2 && !hd->on);
    std::string conflict;
    if (hd && clash) {
        CHECK(InstallMod(*hd, id, contents, state, conflict));
        CHECK(Exists(contents + "/romfs/tex/a.bin") && Exists(contents + "/romfs/tex/b.bin"));
        CHECK(ModPackages(root + "/mods", state, id)[1].on || ModPackages(root + "/mods", state, id)[0].on);
        // the other package would write over a.bin: refused, nothing written
        CHECK(!InstallMod(*clash, id, contents, state, conflict) && conflict == "romfs/tex/a.bin");
        // putting the same package again is fine (its own files)
        CHECK(InstallMod(*hd, id, contents, state, conflict));
        CHECK(RemoveMod(*hd, id, contents, state));
        CHECK(!Exists(contents + "/romfs/tex/a.bin") && !Exists(contents + "/romfs"));
        CHECK(Exists(contents)); // the game's own folder stays
        CHECK(InstallMod(*clash, id, contents, state, conflict));
        CHECK(RemoveMod(*clash, id, contents, state));
    }
    // a file that was there before is never written over
    std::system(("mkdir -p " + contents + "/romfs/tex && echo mine > " + contents + "/romfs/tex/b.bin").c_str());
    if (hd) CHECK(!InstallMod(*hd, id, contents, state, conflict) && conflict == "romfs/tex/b.bin");

    if (failures) {
        std::printf("mods_test: %d failures\n", failures);
        return 1;
    }
    std::printf("mods_test: ok\n");
    return 0;
}
