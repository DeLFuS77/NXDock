// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <string>

namespace nxdock {

struct SystemInfo;

// What the console is, for the library app: in the inventory (only when the user said yes) and in
// a report. Never a user name, an address or a key.
struct ConsoleFacts {
    std::string firmware;   // as the console shows it, "19.0.1"; empty when unknown
    std::string atmosphere; // "1.8.0"; empty when exosphere did not answer
    int emummc = -1;        // the system runs from an emuMMC: 1 yes, 0 no, -1 unknown
    long long sd_free = -1, sd_total = -1, nand_free = -1, nand_total = -1; // bytes; -1 unknown
    std::string sigpatches; // how they were found (FindSigpatches().how); empty when none
    // for a report
    bool applet = false;    // opened as an applet (from the Album), not as an application
    long long mem_total = -1, mem_used = -1; // the memory of NXDock's process, bytes; -1 unknown
    long long when = 0;     // when it was read, seconds since 1970 (UTC)
};

// Reads them on the console (system_info.cpp): the firmware and Atmosphère of `sys`, the rest now.
// Looks at the SD card and asks the system: call it off the interface's thread.
ConsoleFacts ReadConsoleFacts(const SystemInfo& sys);

} // namespace nxdock
