// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// An official update over a game whose program is no longer the one Nintendo made. The update
// reads its base game by places of the base's RomFS section (its indirect table). A game NXDock
// compressed (Compress games, the LZ4 layer) or an optimized one (merged with an update, an
// Application at the version of that update) has that section written again, and smaller: the
// update reads past its end or the wrong bytes, and the console closes the game with error
// 2002-4021 (IndirectStorageCorrupted). What works: leaving the update out, or installing the
// original game first, under the update; Compress games can then merge the two.
// A game NXDock merged and keeps the recipe of is the exception: a newer update is merged with it
// right after it is installed (25G).
//
// Plain C++: tested on the computer.
#include <cstddef>
#include <cstdint>

namespace nxdock {

// What the console has of the game an update is for.
struct BaseGame {
    bool installed = false;
    uint32_t version = 0;    // of the Application installed: above 0, an update is merged into it
    bool compressed = false; // NXDock compressed its program (its mark is the program installed)
    bool merged = false;     // NXDock merged it with an update
    bool recipe = false;     // and keeps the recipe: a newer update can be merged in the same way
};

enum class UpdateOver {
    Fine,        // a game as Nintendo made it, or none installed
    MergedAfter, // NXDock merges it with the game right after it is installed (25G)
    Breaks,      // the game would not start with it: the original goes first, or it stays out
    InGame,      // the game holds this update already (or a newer one): it stays out
};

constexpr UpdateOver UpdateOverGame(const BaseGame& b, uint32_t update_version) {
    if (!b.installed) return UpdateOver::Fine;
    const bool holds_update = b.merged || b.version > 0;
    if (holds_update && update_version <= b.version) return UpdateOver::InGame;
    if (b.merged) return b.recipe ? UpdateOver::MergedAfter : UpdateOver::Breaks;
    if (b.compressed || b.version > 0) return UpdateOver::Breaks;
    return UpdateOver::Fine;
}

// The game a content is of: a game itself (…000), its update (…800) or a DLC (the game's ID plus
// 0x1000 and a number).
constexpr uint64_t GameOf(uint64_t id) {
    const uint64_t low = id & 0xFFF;
    return low == 0 || low == 0x800 ? id & ~uint64_t(0xFFF) : (id - 0x1000) & ~uint64_t(0xFFF);
}

// Which of the waiting jobs goes now, from their title IDs in the order they came (0: not known).
// The first one, unless it is an update or a DLC whose game waits too: then the game goes first,
// so an update is always installed over the game it is made for, whatever order the files came in.
template <class Ids>
constexpr size_t NextToInstall(const Ids& ids) {
    if (ids.size() == 0) return 0;
    const uint64_t first = ids[0];
    if (first == 0 || (first & 0xFFF) == 0) return 0;
    for (size_t i = 1; i < ids.size(); i++) {
        if (ids[i] != 0 && (ids[i] & 0xFFF) == 0 && ids[i] == GameOf(first)) return i;
    }
    return 0;
}

// What the console has of a game (on the console, compress_nx.cpp).
BaseGame BaseGameOf(unsigned long long app);

// Whether the game does not start with that update over it (it is installed, or about to be).
constexpr bool StopsTheGame(UpdateOver o) { return o == UpdateOver::Breaks || o == UpdateOver::InGame; }

// Whether installing the original game first lets the update work: not for one the game holds
// already, which is simply not needed.
constexpr bool OriginalHelps(UpdateOver o) { return o == UpdateOver::Breaks; }

} // namespace nxdock
