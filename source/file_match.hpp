// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// A game file next to what the console has (13A and 15A of the visual test of groups 3 and 4),
// read from its name only ("Game [0100…][v65536].nsz"), never from inside the file; and the marks
// of a list turned the other way (16A). Plain C++, tested on the computer.
#pragma once

#include "space.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace nxdock {

// The title ID and the version in a file name; false without an ID.
bool ParseTitleName(const std::string& name, unsigned long long& id, unsigned& version);

// What the console has, from the scan of Space: each game, update and DLC with its version, and
// the games themselves (an update or a DLC alone is not its game).
struct ConsoleHas {
    std::map<unsigned long long, unsigned> titles;
    std::set<unsigned long long> games;
};
ConsoleHas HasOf(const std::vector<InstalledApp>& apps);

// Unknown: nothing of it is on the console, or it is not a game file. Installed: that version.
// ForGame: an update or a DLC of a game the console has, newer than what is installed or not
// installed at all. Older: older than what is installed.
enum class FileState { Unknown, Installed, ForGame, Older };
struct FileMatch {
    FileState state = FileState::Unknown;
    unsigned installed = 0; // the version the console has of that title
    unsigned running = 0;   // a game: the version it runs, its update's when it has one
};
FileMatch MatchFile(const ConsoleHas& has, unsigned long long id, unsigned version);

// The files of a folder or a shop that would update the games of the console (15A): each update
// and DLC in the state ForGame, only the newest of one title. `left` counts the other updates and
// DLC of those games: older, installed already, or an older copy of one chosen.
struct FoundFile {
    unsigned long long id = 0;
    unsigned version = 0;
};
struct UpdatePick {
    std::vector<size_t> chosen; // indices into the files, in their order
    int left = 0;
};
UpdatePick PickUpdates(const ConsoleHas& has, const std::vector<FoundFile>& files);

// Turns every mark the other way where `can` allows it (16A); `can` empty allows all. How many
// are marked after.
int InvertMarks(std::vector<char>& marks, const std::vector<char>& can = {});
// The same for a set of ids out of those that can be marked.
int InvertMarks(std::set<unsigned long long>& marked, const std::vector<unsigned long long>& can);

} // namespace nxdock
