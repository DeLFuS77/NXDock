// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// A new update over a merged game (25E). An update reads its base game by places of the base's
// RomFS section, so a merged game, whose base is gone, keeps a recipe of where each piece of the
// base it read lies in its own RomFS: the indirect table of the update it was merged with, the runs
// read from the base. A newer update reads mostly the same places of the base (measured on a large
// game: 0 to 1.2 MB missing from one version to the next, 0.04 MB skipping several), so the
// base is made again from the merged game, the few bytes missing are asked of the library on the
// computer, and every block read is checked by the hashes of the new update as patch_view reads it.
//
// Plain C++: tested on the computer.
#include "patch_view.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nxdock::recipe {

// A piece of the RomFS section of the base game: [phys, phys + size) of the base, which is
// [at, at + size) of the RomFS of the merged game.
struct Piece {
    uint64_t phys = 0, size = 0, at = 0;
};

struct Recipe {
    uint32_t version = 0;   // the version of the update the game was merged with
    uint64_t base_size = 0; // the size of the RomFS section of the base, as the updates see it
    std::vector<Piece> pieces; // sorted by phys
};

// From the indirect table of the update a game was merged with: its runs read from the base
// (storage 0) that lie in the data level of the patched section (at data_offset, data_size bytes:
// those are places of the RomFS of the merged game). Runs elsewhere (the hash levels) are left out;
// a newer update that reads them gets them from the library.
Recipe FromIndirect(const std::vector<patchview::IndirectEntry>& entries, uint64_t end, uint64_t data_offset, uint64_t data_size,
                    uint64_t base_size, uint32_t version);

std::vector<uint8_t> Write(const Recipe& r);
bool Read(const std::vector<uint8_t>& bytes, Recipe& r);

// The places of the base a new update reads (from its indirect table) that the recipe has not.
std::vector<std::pair<uint64_t, uint64_t>> Missing(const Recipe& r, const std::vector<patchview::IndirectEntry>& entries, uint64_t end);

// The bytes of the base at [phys, phys + size), from the library (false: not there).
using FetchFn = std::function<bool(uint64_t phys, uint8_t* buf, size_t size)>;

// The base made again: what the recipe has read from the merged game's RomFS (`merged`, the
// caller's), the rest fetched. Read fails where neither has it.
class RebuiltBase : public patchview::Section {
public:
    RebuiltBase(Recipe r, std::function<bool(uint64_t, uint8_t*, size_t)> merged, FetchFn fetch);
    bool Read(uint64_t offset, uint8_t* buf, size_t size) override;
    uint64_t Size() const override { return m_r.base_size; }
    uint64_t Fetched() const { return m_fetched; }

private:
    Recipe m_r;
    std::function<bool(uint64_t, uint8_t*, size_t)> m_merged;
    FetchFn m_fetch;
    uint64_t m_fetched = 0;
};

} // namespace nxdock::recipe
