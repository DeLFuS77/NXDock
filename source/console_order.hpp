// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// What In the console works out without the console (3A, 4A, 5C and 7A of the visual test of In the
// console, group 2): the orders of the list, the play time and when a game was last played as
// words, the updates and DLC without their game, and the folders of the mods of a game. Tested on
// the computer.
#pragma once

#include "space.hpp"

#include <string>
#include <vector>

namespace nxdock {

// The orders of the list (4A): by name, the largest first, the last played first, the most played
// first, and where it is (the SD card, both, the internal memory).
enum : int { kSortName, kSortSize, kSortLast, kSortTime, kSortWhere, kSortCount };

// Sorts as `sort` says. By name the order of the scan stays (it is by name already, and the names
// read later do not move the rows); the others keep that order among equals.
void SortConsole(std::vector<InstalledApp>& apps, int sort);

// An update or a DLC on the console without its game (5C): not archived, and nothing of it is the
// game itself.
bool WithoutGame(const InstalledApp& a);

// The play time as "42 h 10 min", "15 h" or "25 min", with the formats of the language.
std::string PlayTimeText(long long minutes, const char* h_min_fmt, const char* h_fmt, const char* min_fmt);

// How long ago it was played, from the minutes since then: today, yesterday, days, weeks, months.
std::string AgoText(long long minutes, const char* today, const char* yesterday, const char* days_fmt, const char* weeks_fmt,
                    const char* months_fmt);

// The folders of a game in atmosphere/contents/<title ID> (romfs, exefs, cheats…): how many it has;
// -1 when the game has no such folder.
int ModFolders(const std::string& contents_dir);

// The game a title belongs to: an update or a DLC to its game.
unsigned long long AppOfTitle(unsigned long long tid);

// The title of a rights id (its first 8 bytes) and the name of its copy, "<rights id>.tik" (or
// .cert), in hexadecimal digits as the console writes them.
unsigned long long TitleOfRights(const unsigned char rights[16]);
std::string TicketFileName(const unsigned char rights[16], const char* ext);

} // namespace nxdock
