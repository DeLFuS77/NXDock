// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// A game installed at the version of an update is one optimized: its update is merged into it.
// An update of it left on the console is read over it as over the base game it was made for; what
// it reads is no longer there, its RomFS does not match its hashes, and the game closes with an
// error. So installing such a game takes away every update of it at or below its version. A newer
// update stays (the console runs it as it would over the game), and a game at version 0 (as
// Nintendo ships them) keeps its updates.
//
// Plain C++: tested on the computer.
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nxdock {

// The content ID of an NCA.
using ContentId = std::array<uint8_t, 16>;

constexpr uint8_t kMetaApplication = 0x80, kMetaPatch = 0x81;

// The game being installed is one optimized (its update merged in).
constexpr bool IsMergedGame(uint8_t type, uint32_t version) { return type == kMetaApplication && version > 0; }

// An update installed (type, version) is in the game being installed and goes.
constexpr bool UpdateIsInGame(uint8_t game_type, uint32_t game_version, uint8_t type, uint32_t version) {
    return IsMergedGame(game_type, game_version) && type == kMetaPatch && version <= game_version;
}

// An NCA of an update that goes is deleted only when the game being installed does not list it:
// an optimized game keeps the control NCA (and the legal information and HTML documents) of the
// update it was made from, with the same content ID. Deleting it with the update left the game
// without its control NCA, and it could not start.
inline bool ContentInGame(const std::vector<ContentId>& game, const uint8_t* id) {
    for (const auto& kept : game) {
        if (std::memcmp(kept.data(), id, kept.size()) == 0) return true;
    }
    return false;
}

} // namespace nxdock
