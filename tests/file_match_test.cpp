// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
// Groups 3 and 4: a file of the explorer next to what the console has, from its name (13A), the
// updates and DLC of a folder for the games installed (15A), and the marks turned the other way
// (16A).
#include "file_match.hpp"

#include <cstdio>
#include <string>

using namespace nxdock;

static int failed = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        failed++;
    }
}

static InstalledApp App(unsigned long long id, std::vector<InstalledApp::Content> contents, bool archived = false) {
    InstalledApp a;
    a.app_id = id;
    a.name = "game";
    a.archived = archived;
    a.contents = std::move(contents);
    return a;
}

static FileState StateOf(const ConsoleHas& has, const char* name) {
    unsigned long long id = 0;
    unsigned v = 0;
    if (!ParseTitleName(name, id, v)) return FileState::Unknown;
    return MatchFile(has, id, v).state;
}

int main() {
    // the names
    unsigned long long id = 0;
    unsigned v = 0;
    Check(ParseTitleName("Game [0100A1B2C3D4E800][v131072].nsz", id, v) && id == 0x0100A1B2C3D4E800ull && v == 131072, "the ID and version of a name");
    Check(ParseTitleName("Game [0100a1b2c3d4e000].nsp", id, v) && id == 0x0100A1B2C3D4E000ull && v == 0, "small letters, no version");
    Check(!ParseTitleName("Game [US][v65536].nsp", id, v), "no ID, nothing");
    Check(ParseTitleName("Game (2020) [1.0] [0100F2E3D4C5C001][v65536].nsp", id, v) && id == 0x0100F2E3D4C5C001ull && v == 65536,
          "the ID after other brackets");

    // what the console has: a game with its update v262144 and a DLC v65536; a game alone; an
    // update without its game; an archived game
    const unsigned char kApp = 0x80, kPatch = 0x81, kDlc = 0x82;
    const std::vector<InstalledApp> apps = {
        App(0x0100F2E3D4C5B000ull, {{0x0100F2E3D4C5B000ull, 0, kApp, false, 1, 0}, {0x0100F2E3D4C5B800ull, 262144, kPatch, false, 1, 0},
                                    {0x0100F2E3D4C5C001ull, 65536, kDlc, false, 1, 0}}),
        App(0x0100A1B2C3D4E000ull, {{0x0100A1B2C3D4E000ull, 0, kApp, false, 1, 0}}),
        App(0x0100BEEF55AA0000ull, {{0x0100BEEF55AA0800ull, 65536, kPatch, false, 1, 0}}),
        App(0x0100C0FFEE123000ull, {}, true),
    };
    const ConsoleHas has = HasOf(apps);
    Check(has.games.size() == 2, "the games themselves: not an update alone, not an archived game");

    // 13A
    Check(StateOf(has, "Karts [0100F2E3D4C5B000][v0].nsz") == FileState::Installed, "the game installed");
    Check(MatchFile(has, 0x0100F2E3D4C5B000ull, 0).running == 262144, "the game runs its update's version");
    Check(StateOf(has, "Karts [0100F2E3D4C5B800][v131072].nsz") == FileState::Older, "an update older than the one installed");
    Check(StateOf(has, "Karts [0100F2E3D4C5B800][v262144].nsz") == FileState::Installed, "the update installed");
    Check(StateOf(has, "Karts [0100F2E3D4C5B800][v327680].nsz") == FileState::ForGame, "a newer update for the game");
    Check(StateOf(has, "Karts DLC [0100F2E3D4C5C002].nsz") == FileState::ForGame, "a DLC not installed of a game installed");
    Check(StateOf(has, "Karts DLC [0100F2E3D4C5C001][v65536].nsz") == FileState::Installed, "the DLC installed");
    Check(StateOf(has, "Karts DLC [0100F2E3D4C5C001][v0].nsz") == FileState::Older, "an older DLC");
    Check(StateOf(has, "Forest [0100A1B2C3D4E800][v65536].nsz") == FileState::ForGame, "the first update of a game");
    Check(StateOf(has, "Fight [0100BEEF55AA0800][v131072].nsz") == FileState::Unknown, "a newer update without its game: nothing");
    Check(StateOf(has, "Fight [0100BEEF55AA0800][v65536].nsz") == FileState::Installed, "but the same version is installed");
    Check(StateOf(has, "Puzzle [0100C0FFEE123000][v0].nsp") == FileState::Unknown, "an archived game is not installed");
    Check(StateOf(has, "Other [0100999999999000][v0].nsp") == FileState::Unknown, "a game not on the console");
    Check(StateOf(has, "readme.txt") == FileState::Unknown, "not a game");

    // 15A: the newest of each, the DLC not installed; the rest left alone
    const std::vector<FoundFile> files = {
        {0x0100F2E3D4C5B000ull, 0},      // the game: not counted
        {0x0100F2E3D4C5B800ull, 131072}, // older: left
        {0x0100F2E3D4C5B800ull, 327680}, // newer: chosen
        {0x0100F2E3D4C5B800ull, 393216}, // newer still: chosen instead
        {0x0100F2E3D4C5C001ull, 65536},  // installed: left
        {0x0100F2E3D4C5C002ull, 0},      // a DLC not installed: chosen
        {0x0100A1B2C3D4E800ull, 65536},  // the first update of the other game: chosen
        {0x0100BEEF55AA0800ull, 131072}, // without its game: not counted
        {0x0100999999999800ull, 65536},  // a game not on the console: not counted
        {0, 0},                          // not a game
    };
    const UpdatePick pick = PickUpdates(has, files);
    Check(pick.chosen == std::vector<size_t>({3, 5, 6}), "the newest update, the DLC missing, the other game's update");
    Check(pick.left == 3, "the older, the installed and the older copy are left alone");
    Check(PickUpdates(has, {}).chosen.empty(), "nothing found, nothing chosen");

    // 16A
    std::vector<char> marks = {1, 0, 0, 1, 0};
    Check(InvertMarks(marks) == 3 && marks == std::vector<char>({0, 1, 1, 0, 1}), "every mark the other way");
    marks = {0, 1, 0};
    Check(InvertMarks(marks, {0, 1, 1}) == 1 && marks == std::vector<char>({0, 0, 1}), "what cannot be marked stays without one");
    std::set<unsigned long long> picked = {1, 3};
    Check(InvertMarks(picked, {1, 2, 3, 4}) == 2 && picked == std::set<unsigned long long>({2, 4}), "the games marked the other way");
    picked = {9};
    Check(InvertMarks(picked, {1}) == 1 && picked == std::set<unsigned long long>({1}), "a mark out of the list goes");

    if (failed) return 1;
    std::printf("file_match_test: ok\n");
    return 0;
}
