// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The file list's tools on the computer: the folders NXDock never touches, names, counting, deleting,
// copying with free names, moving, splitting a large file for FAT32, a folder into itself, the game
// files under folders, and the list of every file.
#include "file_source.hpp"
#include "fileops.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

extern "C" void log_write(const char*, ...) {}

using namespace nxdock;
using namespace nxdock::fileops;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void Write(const std::string& path, const std::string& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

static std::string Read(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "<none>";
    std::string s;
    char buf[256];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

static bool There(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

int main(int argc, char** argv) {
    // guards
    CHECK(GuardOf("sdmc:/") == Guard::System);
    CHECK(GuardOf("sdmc:/Nintendo") == Guard::System);
    CHECK(GuardOf("sdmc:/Nintendo/Contents/registered") == Guard::System);
    CHECK(GuardOf("sdmc:/emuMMC/RAW1") == Guard::System);
    CHECK(GuardOf("sdmc:/bootloader/hekate_ipl.ini") == Guard::System);
    CHECK(GuardOf("sdmc:/atmosphere") == Guard::System);
    CHECK(GuardOf("sdmc:/atmosphere/contents/0100000000001000") == Guard::Careful);
    CHECK(GuardOf("sdmc:/switch") == Guard::System);
    CHECK(GuardOf("sdmc:/switch/NXDock") == Guard::Self);
    CHECK(GuardOf("sdmc:/switch/NXDock/NXDock.nro") == Guard::Self);
    CHECK(GuardOf("sdmc:/switch/NXDock/mods") == Guard::Free);
    CHECK(GuardOf("sdmc:/config/NXDock/install.ini") == Guard::Self);
    CHECK(GuardOf("sdmc:/switch/other.nro") == Guard::Free);
    CHECK(GuardOf("sdmc:/games/a.nsp") == Guard::Free);
    CHECK(GuardOf("ums0:/Nintendo") == Guard::Free); // a USB disk is the user's own
    CHECK(GuardOfAll({"sdmc:/games", "sdmc:/atmosphere/x"}) == Guard::Careful);

    // names and paths
    CHECK(ValidName("Game (EU).nsp"));
    CHECK(!ValidName("a/b"));
    CHECK(!ValidName(".."));
    CHECK(!ValidName("what?"));
    CHECK(!ValidName(""));
    CHECK(!ValidName("ends with dot."));
    CHECK(Junk("._game.nsp"));
    CHECK(Junk("Thumbs.db"));
    CHECK(Junk("$RECYCLE.BIN"));
    CHECK(!Junk("game.nsp"));
    CHECK(Parent("sdmc:/a/b/") == "sdmc:/a/");
    CHECK(Parent("sdmc:/a/b") == "sdmc:/a/");
    CHECK(NameOf("sdmc:/a/b/") == "b");
    CHECK(DeviceOf("ums0:/x") == "ums0:");
    CHECK(Within("sdmc:/a/b/c", "sdmc:/a/b"));
    CHECK(Within("sdmc:/a/b", "sdmc:/a/b/"));
    CHECK(!Within("sdmc:/a/bc", "sdmc:/a/b"));

    const std::string dir = std::string(argc > 1 ? argv[1] : "/tmp") + "/fileops_test";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir + "/src/sub " + dir + "/dst").c_str());
    Write(dir + "/src/a.txt", "hello");
    Write(dir + "/src/sub/b.bin", "0123456789");
    Write(dir + "/src/Game [0100000000010000][v0].nsp", "GAMEDATA");

    // counting
    const Count c = CountTree(dir + "/src");
    CHECK(c.files == 3);
    CHECK(c.bytes == 5 + 10 + 8);
    CHECK(c.folders == 2);

    // the list of every file, and the games only
    const auto all = ListFolder(dir + "/src", true, true);
    CHECK(all.size() == 3); // sub, a.txt, the game
    const auto games = ListFolder(dir + "/src", true, false);
    CHECK(games.size() == 2); // sub and the game

    // copy, then copy again: free names
    {
        Progress p;
        CHECK(CopyAll({dir + "/src"}, dir + "/dst", false, false, p));
        CHECK(p.done && !p.failed);
        CHECK(p.files_done == 3);
        CHECK(Read(dir + "/dst/src/sub/b.bin") == "0123456789");
        CHECK(There(dir + "/src/a.txt")); // a copy keeps the original
    }
    {
        Progress p;
        CHECK(CopyAll({dir + "/src/a.txt"}, dir + "/dst", false, false, p));
        CHECK(CopyAll({dir + "/src/a.txt"}, dir + "/dst", false, false, p));
        CHECK(Read(dir + "/dst/a.txt") == "hello");
        CHECK(Read(dir + "/dst/a (2).txt") == "hello");
        CHECK(FreeName(dir + "/dst", "a.txt") == "a (3).txt");
        CHECK(FreeName(dir + "/dst", "src") == "src (2)");
    }
    // a folder into itself is refused
    {
        Progress p;
        CHECK(!CopyAll({dir + "/src"}, dir + "/src/sub", false, false, p));
        CHECK(p.failed);
        CHECK(!There(dir + "/src/sub/src"));
    }
    // a large file split for FAT32 (limits made small): parts 00, 01, 02 in a folder of its name
    {
        Write(dir + "/big.nsp", "AAAABBBBCC");
        Progress p;
        CHECK(CopyAll({dir + "/big.nsp"}, dir + "/dst", false, true, p, 5, 4));
        CHECK(Read(dir + "/dst/big.nsp/00") == "AAAA");
        CHECK(Read(dir + "/dst/big.nsp/01") == "BBBB");
        CHECK(Read(dir + "/dst/big.nsp/02") == "CC");
        // and NXDock lists it as one game file
        const auto split = ListFolder(dir + "/dst", true);
        bool found = false;
        for (const auto& e : split) found |= e.name == "big.nsp" && !e.dir && e.parts.size() == 3 && e.size == 10;
        CHECK(found);
        // not split when it fits
        Progress q;
        CHECK(CopyAll({dir + "/big.nsp"}, dir + "/src", false, true, q, 100, 4));
        CHECK(Read(dir + "/src/big.nsp") == "AAAABBBBCC");
    }
    // the games under folders, a split folder as one game
    {
        const auto found = GamesUnder({dir + "/dst"});
        int nsp = 0;
        for (const auto& g : found) nsp += g.format == "nsp";
        CHECK(nsp == 2); // dst/big.nsp (split) and dst/src/Game…nsp
        const auto one = GamesUnder({dir + "/dst/big.nsp"});
        CHECK(one.size() == 1 && one[0].parts.size() == 3);
    }
    // move (one device: a rename)
    {
        Progress p;
        CHECK(CopyAll({dir + "/src/a.txt"}, dir + "/dst/src/sub", true, false, p));
        CHECK(!There(dir + "/src/a.txt"));
        CHECK(Read(dir + "/dst/src/sub/a.txt") == "hello");
    }
    // a new folder and a new name
    CHECK(MakeFolder(dir + "/dst", "New"));
    CHECK(!MakeFolder(dir + "/dst", "New"));
    CHECK(!MakeFolder(dir + "/dst", "bad:name"));
    CHECK(Rename(dir + "/dst/New", "Renamed"));
    CHECK(There(dir + "/dst/Renamed") && !There(dir + "/dst/New"));
    CHECK(!Rename(dir + "/dst/Renamed", "a.txt")); // taken
    // delete with what it holds
    {
        Progress p;
        CHECK(DeleteAll({dir + "/dst/src", dir + "/dst/a.txt"}, p));
        CHECK(!There(dir + "/dst/src") && !There(dir + "/dst/a.txt"));
        CHECK(p.files_done == 5); // src/a.txt, sub/b.bin, sub/a.txt, the game, and dst/a.txt
    }
    // a stop: nothing more is deleted
    {
        Progress p;
        p.cancel = true;
        CHECK(!DeleteAll({dir + "/dst/Renamed"}, p));
        CHECK(There(dir + "/dst/Renamed"));
        CHECK(!p.failed);
    }

    std::system(("rm -rf " + dir).c_str());
    if (failures) {
        std::printf("fileops_test: %d failed\n", failures);
        return 1;
    }
    std::printf("fileops_test: ok\n");
    return 0;
}
