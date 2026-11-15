// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// What tells a shortcut of homebrew on the HOME menu (a forwarder: RetroArch, DBI, NXDock's own)
// from a game, for In the console: tested on the computer (tests/dialog_fit_test.cpp).
#include <cstdint>

namespace nxdock {

enum class ShortcutSign {
    None,  // a game, or an application of the eShop
    Id,    // its ID does not start with 01, as those of Nintendo's titles do (NXDock's own: 05…)
    Small, // an ID of 01…, but a program of a few MiB with no update and no DLC: a forwarder made
           // with that kind of ID (no game is that small)
};

inline ShortcutSign ShortcutOf(uint64_t app_id, uint64_t program_bytes, bool has_update, bool has_dlc) {
    if ((app_id >> 56) != 0x01) return ShortcutSign::Id;
    if (program_bytes > 0 && program_bytes < (4ull << 20) && !has_update && !has_dlc) return ShortcutSign::Small;
    return ShortcutSign::None;
}

inline const char* ShortcutWhy(ShortcutSign s) {
    return s == ShortcutSign::Id ? "its ID does not start with 01" : s == ShortcutSign::Small ? "a program under 4 MiB, with no update nor DLC" : "a game";
}

} // namespace nxdock
