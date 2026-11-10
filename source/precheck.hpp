// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <string>

namespace nxdock {

// What the console runs, read once when NXDock opens (ReadSystemInfo, on the console).
// Firmwares are in the form of the cnmt: major << 26 | minor << 20 | micro << 16.
struct SystemInfo {
    unsigned firmware = 0;    // 0: unknown
    bool atmosphere = false;  // exosphere answered
    unsigned ams_major = 0, ams_minor = 0, ams_micro = 0;
    unsigned ams_target = 0;  // the newest firmware this Atmosphère supports, 0 unknown
    int key_generation = -1;  // the newest master key the console has, -1 unknown
};

SystemInfo ReadSystemInfo();

// The master key a firmware brings (0 for 1.0.0 … 20 for 21.0.0).
int KeyGenerationOf(unsigned firmware);

// The check before installing (2A): three lines, each fine, a warning, or unknown. Only
// warns: NXDock never installs firmware or sigpatches.
struct PreCheck {
    enum class State { Fine, Warn, Unknown };
    struct Line {
        State state = State::Unknown;
        std::string right; // what was found, in the console language
    };
    Line firmware, keys, atmosphere;
    bool Warns() const { return firmware.state == State::Warn || keys.state == State::Warn || atmosphere.state == State::Warn; }
};

// `required` is the newest system version the queued contents ask for (0: none).
PreCheck CheckBeforeInstall(const SystemInfo& info, unsigned required);

} // namespace nxdock
