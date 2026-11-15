// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The rows a dialog shows so its buttons stay on a 720 p screen, and "1 update, 2 DLC".
#include "dialog_fit.hpp"
#include "shortcut_filter.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failed = 0;

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

} // namespace

int main() {
    using nxdock::CountsText;
    using nxdock::RowsThatFit;
    // 30 games with both boxes to tick: 5 rows do not fit in 592 px, fewer do, the buttons stay
    const float fixed = 72 + 70 + 28 + 2 * 98 + 52 + 24, row = 40, more = 30, room = 592;
    const int shown = RowsThatFit(30, 5, fixed, row, more, room);
    Check(shown < 5 && shown >= 1, "a long list with two boxes shows fewer rows");
    Check(fixed + shown * row + more <= room, "and the dialog fits on the screen");
    Check(fixed + (shown + 1) * row + more > room, "with as many rows as fit");
    Check(RowsThatFit(3, 5, 200, 40, 30, 592) == 3, "a short list is shown whole");
    Check(RowsThatFit(30, 5, 200, 40, 30, 592) == 5, "five rows when there is room");
    Check(RowsThatFit(30, 5, 900, 40, 30, 592) == 1, "one row at least");
    Check(RowsThatFit(0, 5, 200, 40, 30, 592) == 0, "no rows, no list");
    // the counts: singular for one, nothing for none
    const auto es = [](int u, int d) { return CountsText(u, d, "1 actualización", "%d actualizaciones", "1 DLC", "%d DLC", ", "); };
    Check(es(1, 0) == "1 actualización", "one update in the singular");
    Check(es(2, 0) == "2 actualizaciones", "two in the plural");
    Check(es(0, 3) == "3 DLC", "no \"0 actualizaciones\"");
    Check(es(1, 1) == "1 actualización, 1 DLC", "both, joined");
    Check(es(0, 0).empty(), "nothing for none");
    // the shortcuts of homebrew: by their ID, or a forwarder with an ID of 01… (tiny, nothing else)
    using nxdock::ShortcutOf;
    using nxdock::ShortcutSign;
    const unsigned long long mib = 1ull << 20;
    Check(ShortcutOf(0x0500000000001000ull, 2 * mib, false, false) == ShortcutSign::Id, "NXDock's own shortcut (05...) is hidden");
    Check(ShortcutOf(0x0400000000010000ull, 3000 * mib, false, false) == ShortcutSign::Id, "any ID not of 01... is hidden");
    Check(ShortcutOf(0x01000320000CC000ull, 1400 * mib, true, false) == ShortcutSign::None, "a game stays");
    Check(ShortcutOf(0x0100000000010000ull, 90 * mib, false, false) == ShortcutSign::None, "a small game of the eShop stays");
    Check(ShortcutOf(0x0100000000010000ull, 2 * mib, false, false) == ShortcutSign::Small, "a forwarder with an ID of 01... is hidden");
    Check(ShortcutOf(0x0100000000010000ull, 2 * mib, true, false) == ShortcutSign::None, "but not with an update");
    Check(ShortcutOf(0x0100000000010000ull, 0, false, true) == ShortcutSign::None, "nor a DLC alone");
    if (g_failed) {
        std::printf("%d failed\n", g_failed);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
