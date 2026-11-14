// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
// In the console, group 2: the orders of the list, the play time and how long ago in words, the
// updates and DLC without their game, the folders of the mods of a game and the names of the
// copies of the tickets.
#include "console_order.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace nxdock;

static int failed = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        failed++;
    }
}

static InstalledApp App(const char* name, unsigned long long sd, unsigned long long nand, long long idle, long long played) {
    InstalledApp a;
    a.name = name;
    a.sd_bytes = sd;
    a.nand_bytes = nand;
    a.idle_minutes = idle;
    a.play_minutes = played;
    a.contents.push_back({0x0100000000010000ull, 0, 0x80, nand > 0, sd + nand, 0});
    return a;
}

static std::string Names(const std::vector<InstalledApp>& apps) {
    std::string out;
    for (const auto& a : apps) out += a.name;
    return out;
}

int main(int argc, char** argv) {
    const std::string tmp = argc > 1 ? argv[1] : "/tmp";
    // the orders (4A)
    const std::vector<InstalledApp> scan = {App("A", 10, 0, 60, 300), App("B", 50, 0, -1, 0), App("C", 0, 30, 5, 900), App("D", 20, 20, 2000, 10)};
    auto apps = scan;
    SortConsole(apps, kSortName);
    Check(Names(apps) == "ABCD", "by name the order of the scan stays");
    apps = scan;
    SortConsole(apps, kSortSize);
    Check(Names(apps) == "BDCA", "the largest first");
    apps = scan;
    SortConsole(apps, kSortLast);
    Check(Names(apps) == "CADB", "the last played first, never played at the end");
    apps = scan;
    SortConsole(apps, kSortTime);
    Check(Names(apps) == "CADB", "the most played first");
    apps = scan;
    SortConsole(apps, kSortWhere);
    Check(Names(apps) == "ABDC", "the SD card, both, then the internal memory");
    apps = scan;
    SortConsole(apps, 99);
    Check(Names(apps) == "ABCD", "an order not known changes nothing");

    // without the game (5C)
    InstalledApp lone;
    lone.contents.push_back({0x0100000000010800ull, 65536, 0x81, false, 5, 0});
    Check(WithoutGame(lone), "an update alone has no game");
    Check(!WithoutGame(scan[0]), "a game has its game");
    InstalledApp archived;
    archived.archived = true;
    Check(!WithoutGame(archived), "an archived game is not an update without its game");

    // the play time and how long ago (3A)
    Check(PlayTimeText(2530, "%d h %d min", "%d h", "%d min") == "42 h 10 min", "hours and minutes");
    Check(PlayTimeText(900, "%d h %d min", "%d h", "%d min") == "15 h", "whole hours");
    Check(PlayTimeText(25, "%d h %d min", "%d h", "%d min") == "25 min", "minutes");
    Check(PlayTimeText(-1, "%d h %d min", "%d h", "%d min") == "0 min", "not known is nothing");
    const auto ago = [](long long minutes) { return AgoText(minutes, "today", "yesterday", "%d days ago", "%d weeks ago", "%d months ago"); };
    Check(ago(30) == "today", "today");
    Check(ago(60 * 24 + 5) == "yesterday", "yesterday");
    Check(ago(3 * 60 * 24) == "3 days ago", "days");
    Check(ago(15 * 60 * 24) == "2 weeks ago", "weeks");
    Check(ago(90 * 60 * 24) == "3 months ago", "months");

    // the folders of mods of a game (7A)
    const std::string dir = tmp + "/console_order_mods/0100000000010000";
    std::system(("rm -rf " + tmp + "/console_order_mods").c_str());
    Check(ModFolders(dir) == -1, "no folder: nothing to offer");
    std::system(("mkdir -p " + dir + "/romfs " + dir + "/cheats && touch " + dir + "/note.txt").c_str());
    Check(ModFolders(dir) == 2, "its folders, not its files");

    // titles and tickets (6A)
    Check(AppOfTitle(0x0100000000010800ull) == 0x0100000000010000ull, "an update belongs to its game");
    Check(AppOfTitle(0x0100000000011001ull) == 0x0100000000010000ull, "a DLC belongs to its game");
    Check(AppOfTitle(0x0100000000010000ull) == 0x0100000000010000ull, "a game is its own");
    const unsigned char rights[16] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0, 0, 0, 0, 0, 0, 0, 0x11};
    Check(TitleOfRights(rights) == 0x0100000000010800ull, "the title of a rights id");
    Check(TicketFileName(rights, ".tik") == "01000000000108000000000000000011.tik", "the name of its copy");
    if (failed) return 1;
    std::printf("console_order_test: ok\n");
    return 0;
}
