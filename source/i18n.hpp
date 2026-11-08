// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

namespace nxdock {

struct Strings {
    const char *library, *queue, *installed, *settings;
    const char *free_space, *all, *not_installed, *with_update, *dlc;
    const char *search, *view, *exit, *back;
    const char *installing, *more_in_queue, *left;
    const char *network, *decompress, *threads, *write_sd, *check_sha, *parts_of;
    const char *in_queue, *waiting, *done, *checked, *no_sleep, *pause, *remove;
    const char *base_update_sd;
    const char *theme, *dark, *light, *change;
    const char *coming_soon, *preview_note;
    const char *update_badge;
};

// Picks English or Spanish from the console language; more languages later.
const Strings& Tr();
void InitStrings();

} // namespace nxdock
