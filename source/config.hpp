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
    std::string pin;    // with https: the hash of the app's public key, taken when first linked
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

// How the interface behaves, in sdmc:/config/NXDock/ui.ini.
struct UiPrefs {
    bool wizard_done = false;   // the first-run wizard was shown (or skipped)
    bool wizard_music = true;   // the soft music of the wizard
    bool sounds = true;         // the sounds of the interface
    bool finish_effect = true;  // when the queue ends: notice, vibration and sound
    bool battery_notice = true; // a notice before installing with little battery and no charger
    int battery_level = 15;     // from which %
    bool check_updates = true;  // at start, say how many updates the library has for what is installed
    bool saver = true;          // dims the screen after a while without touching anything while installing
    bool screen_off_auto = false; // turns the screen off when a long queue starts (11A)
    int anim = 0;               // the animations: 0 full, 1 gentle, 2 none
    int language = -1;          // -1: the console's; else an index of the languages NXDock speaks
    bool theme_auto = false;    // the theme follows the console's
    int dest = 0;               // where the page of a game installs: 0 SD card, 1 internal memory, 2 ask each time
    int pick = 0;               // what the page of a game ticks: 0 what is missing, 1 the game only, 2 the game and its update
    int sort = 0;               // the order of the library: 0 A-Z, 1 size, 2 not installed first, 3 recent
    std::string focus;          // the key of the game in focus in the library, for the next time

    static UiPrefs Load();
    bool Save() const;
};

} // namespace nxdock
