// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The rows a dialog shows so its buttons stay on a 720 p screen, and "1 update, 2 DLC".
#include "dialog_fit.hpp"

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
    if (g_failed) {
        std::printf("%d failed\n", g_failed);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
