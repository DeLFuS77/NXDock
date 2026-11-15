// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// Plain pieces of the dialogs, tested on the computer (tests/dialog_fit_test.cpp).
#include <algorithm>
#include <cstdio>
#include <string>

namespace nxdock {

// How many rows of a dialog's list it shows: at most `cap`, and fewer while the dialog would be
// taller than `room` (its buttons always on the screen); one at least. `fixed`: the dialog without
// its rows; `more_h`: the line "and N more" under them when some are left out.
inline int RowsThatFit(int rows, int cap, float fixed, float row_h, float more_h, float room) {
    int shown = std::min(rows, cap);
    const auto height = [&](int n) { return fixed + float(n) * row_h + (rows > n ? more_h : 0.f); };
    while (shown > 1 && height(shown) > room) shown--;
    return std::max(0, shown);
}

// "1 update, 2 DLC": only the parts there are, the singular for one. Empty when there are none.
inline std::string CountsText(int updates, int dlc, const char* update_one, const char* updates_fmt, const char* dlc_one, const char* dlc_fmt,
                              const char* sep) {
    std::string out;
    char text[96];
    if (updates > 0) {
        if (updates == 1) std::snprintf(text, sizeof(text), "%s", update_one);
        else std::snprintf(text, sizeof(text), updates_fmt, updates);
        out = text;
    }
    if (dlc > 0) {
        if (dlc == 1) std::snprintf(text, sizeof(text), "%s", dlc_one);
        else std::snprintf(text, sizeof(text), dlc_fmt, dlc);
        if (!out.empty()) out += sep;
        out += text;
    }
    return out;
}

} // namespace nxdock
