// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// A shortcut to NXDock on the HOME menu (a forwarder): a small application installed to the SD
// card that opens NXDock.nro as a game does, with all the memory and the network of a game,
// instead of the little an applet (the Album) gets. Its NCAs are made on the console with the
// console's own header key, so nothing secret leaves it; made again, it is updated in place.
// Based on Sphaira's owo.cpp (GPL-3.0), see NOTICE.
#include <switch.h>

#include <string>

namespace nxdock::forwarder {

// what is not a call that failed
constexpr Result kResultNoNro = MAKERESULT(505, 1);   // the .nro is missing, or has no icon or NACP
constexpr Result kResultRunning = MAKERESULT(505, 2); // NXDock runs from the shortcut
constexpr Result kResultNpdm = MAKERESULT(505, 3);    // the loader's NPDM could not be set

struct Status {
    std::string nro;          // the NXDock.nro the shortcut opens
    u64 title_id = 0;         // the shortcut's title id (from the path of the .nro)
    bool nro_found = false;   // the .nro is where the shortcut would point
    bool installed = false;   // the shortcut is on the HOME menu
    bool running_from = false; // NXDock was opened from the shortcut
};

// Where NXDock is and whether its shortcut is installed.
Status Query();

// Installs the shortcut to the SD card, or updates it. Not while NXDock runs from it.
Result Install();

// Takes the shortcut off the HOME menu. Not while NXDock runs from it.
Result Remove();

} // namespace nxdock::forwarder
