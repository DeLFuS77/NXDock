// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace nxdock {
namespace {

constexpr const char* kDir = "sdmc:/config/NXDock";
constexpr const char* kPath = "sdmc:/config/NXDock/config.ini";
constexpr const char* kTemp = "sdmc:/config/NXDock/config.ini.tmp";
constexpr const char* kInstallPath = "sdmc:/config/NXDock/install.ini";
constexpr const char* kInstallTemp = "sdmc:/config/NXDock/install.ini.tmp";
constexpr const char* kUiPath = "sdmc:/config/NXDock/ui.ini";
constexpr const char* kUiTemp = "sdmc:/config/NXDock/ui.ini.tmp";

std::string Trim(const char* s) {
    std::string out(s);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

} // namespace

Config Config::Load() {
    Config c;
    FILE* f = std::fopen(kPath, "r");
    if (!f) {
        return c;
    }
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        if (!std::strncmp(line, "url=", 4)) c.url = Trim(line + 4);
        else if (!std::strncmp(line, "key=", 4)) c.key = Trim(line + 4);
        else if (!std::strncmp(line, "id=", 3)) c.id = Trim(line + 3);
        else if (!std::strncmp(line, "name=", 5)) c.name = Trim(line + 5);
        else if (!std::strncmp(line, "pin=", 4)) c.pin = Trim(line + 4);
        else if (!std::strncmp(line, "theme=", 6)) c.light = Trim(line + 6) == "light";
    }
    std::fclose(f);
    return c;
}

bool Config::Save() const {
    mkdir("sdmc:/config", 0777);
    mkdir(kDir, 0777);
    // written whole to a new file and then put in place: a power cut never leaves half of it
    FILE* f = std::fopen(kTemp, "w");
    if (!f) {
        return false;
    }
    std::fprintf(f, "url=%s\nkey=%s\nid=%s\nname=%s\ntheme=%s\n", url.c_str(), key.c_str(), id.c_str(), name.c_str(),
                 light ? "light" : "dark");
    if (!pin.empty()) {
        std::fprintf(f, "pin=%s\n", pin.c_str());
    }
    const bool ok = std::fclose(f) == 0;
    if (!ok) {
        std::remove(kTemp);
        return false;
    }
    std::remove(kPath);
    return std::rename(kTemp, kPath) == 0;
}

InstallPrefs InstallPrefs::Load() {
    InstallPrefs p;
    FILE* f = std::fopen(kInstallPath, "r");
    if (!f) {
        return p;
    }
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        if (!std::strncmp(line, "connections=", 12)) p.connections = std::atoi(line + 12);
        else if (!std::strncmp(line, "download_test=", 14)) p.download_test = std::atoi(line + 14) != 0;
        else if (!std::strncmp(line, "share_installed=", 16)) p.share_installed = std::atoi(line + 16);
    }
    std::fclose(f);
    return p;
}

bool InstallPrefs::Save() const {
    mkdir("sdmc:/config", 0777);
    mkdir(kDir, 0777);
    FILE* f = std::fopen(kInstallTemp, "w");
    if (!f) {
        return false;
    }
    std::fprintf(f, "connections=%d\n", connections);
    if (download_test) {
        std::fprintf(f, "download_test=1\n");
    }
    if (share_installed >= 0) {
        std::fprintf(f, "share_installed=%d\n", share_installed);
    }
    const bool ok = std::fclose(f) == 0;
    if (!ok) {
        std::remove(kInstallTemp);
        return false;
    }
    std::remove(kInstallPath);
    return std::rename(kInstallTemp, kInstallPath) == 0;
}

UiPrefs UiPrefs::Load() {
    UiPrefs p;
    FILE* f = std::fopen(kUiPath, "r");
    if (!f) {
        return p;
    }
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        const char* eq = std::strchr(line, '=');
        if (!eq) {
            continue;
        }
        const std::string key(line, eq - line);
        const std::string value = Trim(eq + 1);
        const int n = std::atoi(value.c_str());
        if (key == "wizard_done") p.wizard_done = n != 0;
        else if (key == "wizard_music") p.wizard_music = n != 0;
        else if (key == "sounds") p.sounds = n != 0;
        else if (key == "finish_effect") p.finish_effect = n != 0;
        else if (key == "battery_notice") p.battery_notice = n != 0;
        else if (key == "battery_level") p.battery_level = n >= 5 && n <= 50 ? n : 15;
        else if (key == "check_updates") p.check_updates = n != 0;
        else if (key == "saver") p.saver = n != 0;
        else if (key == "screen_off_auto") p.screen_off_auto = n != 0;
        else if (key == "anim") p.anim = int(n);
        else if (key == "language") p.language = n;
        else if (key == "theme_auto") p.theme_auto = n != 0;
        else if (key == "dest") p.dest = n >= 0 && n <= 2 ? n : 0;
        else if (key == "pick") p.pick = n >= 0 && n <= 2 ? n : 0;
        else if (key == "sort") p.sort = n >= 0 && n <= 3 ? n : 0;
        else if (key == "focus") p.focus = value;
    }
    std::fclose(f);
    return p;
}

bool UiPrefs::Save() const {
    mkdir("sdmc:/config", 0777);
    mkdir(kDir, 0777);
    FILE* f = std::fopen(kUiTemp, "w");
    if (!f) {
        return false;
    }
    std::fprintf(f,
                 "wizard_done=%d\nwizard_music=%d\nsounds=%d\nfinish_effect=%d\nbattery_notice=%d\nbattery_level=%d\n"
                 "check_updates=%d\nsaver=%d\nlanguage=%d\ntheme_auto=%d\ndest=%d\npick=%d\nsort=%d\nfocus=%s\nscreen_off_auto=%d\nanim=%d\n",
                 wizard_done, wizard_music, sounds, finish_effect, battery_notice, battery_level, check_updates, saver,
                 language, theme_auto, dest, pick, sort, focus.c_str(), screen_off_auto, anim);
    const bool ok = std::fclose(f) == 0;
    if (!ok) {
        std::remove(kUiTemp);
        return false;
    }
    std::remove(kUiPath);
    return std::rename(kUiTemp, kUiPath) == 0;
}

} // namespace nxdock
