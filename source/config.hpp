// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <string>

namespace nxdock {

// What NXDock keeps on the SD card, in sdmc:/config/NXDock/config.ini.
// The key of the console is written there by NXDock itself and never shown on screen or logged.
struct Config {
    std::string url;    // the library app, e.g. http://192.168.1.10:3000
    std::string key;    // the console key the app gave when paired (or typed by hand)
    std::string id;     // the id of the app, to find it again if its address changes
    std::string name;   // the name of the key in the app, shown in Settings
    bool light = false; // light theme

    bool Linked() const { return !url.empty() && !key.empty(); }
    static Config Load();
    bool Save() const;
};

// How NXDock installs, in sdmc:/config/NXDock/install.ini.
struct InstallPrefs {
    int connections = 0;        // best of the last speed test (1, 2 or 4); 0: never measured
    bool download_test = false; // download_test=1: only download and check, nothing is written
    int share_installed = -1;   // share what is installed with the app: -1 not asked yet, 0 no, 1 yes

    static InstallPrefs Load();
    bool Save() const;
};

} // namespace nxdock
