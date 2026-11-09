// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// What the yati core asked Sphaira's App about the console, answered by NXDock.
#pragma once

#include "defines.hpp"

namespace sphaira::env {

// An applet (opened from the Album) has little memory: smaller buffers.
bool IsAppletMode();

// emuMMC kept as files on the SD card: Sphaira writes in smaller pieces with a pause there,
// because the SD card is busy twice. Asked once from Atmosphere, false elsewhere.
bool IsFileBaseEmummc();

} // namespace sphaira::env
