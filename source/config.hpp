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

} // namespace nxdock
