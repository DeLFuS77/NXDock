// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "precheck.hpp"

#include "errors.hpp"
#include "i18n_more.hpp"

#include <algorithm>
#include <cstdio>

namespace nxdock {
namespace {

constexpr unsigned Fw(unsigned major, unsigned minor, unsigned micro) {
    return major << 26 | minor << 20 | micro << 16;
}

// The firmware each master key came with.
constexpr unsigned kKeyFirmware[] = {
    Fw(1, 0, 0),  Fw(3, 0, 0),  Fw(3, 0, 1),  Fw(4, 0, 0),  Fw(5, 0, 0),  Fw(6, 0, 0),  Fw(6, 2, 0),
    Fw(7, 0, 0),  Fw(8, 1, 0),  Fw(9, 0, 0),  Fw(9, 1, 0),  Fw(12, 1, 0), Fw(13, 0, 0), Fw(14, 0, 0),
    Fw(15, 0, 0), Fw(16, 0, 0), Fw(17, 0, 0), Fw(18, 0, 0), Fw(19, 0, 0), Fw(20, 0, 0), Fw(21, 0, 0),
};

std::string Printf(const char* fmt, const std::string& a, const std::string& b = {}, const std::string& c = {}) {
    char t[256];
    std::snprintf(t, sizeof(t), fmt, a.c_str(), b.c_str(), c.c_str());
    return t;
}

std::string Printf(const char* fmt, int a, int b = 0) {
    char t[160];
    std::snprintf(t, sizeof(t), fmt, a, b);
    return t;
}

} // namespace

int KeyGenerationOf(unsigned firmware) {
    int gen = 0;
    for (int i = 0; i < int(sizeof(kKeyFirmware) / sizeof(kKeyFirmware[0])); i++) {
        if ((firmware & 0xFFFF0000) >= kKeyFirmware[i]) gen = i;
    }
    return gen;
}

PreCheck CheckBeforeInstall(const SystemInfo& info, unsigned required) {
    const More& m = TrM();
    PreCheck p;
    required &= 0xFFFF0000;
    const unsigned console = info.firmware & 0xFFFF0000;

    if (!console) {
        p.firmware.right = m.pc_unknown;
    } else if (required && required > console) {
        p.firmware.state = PreCheck::State::Warn;
        p.firmware.right = Printf(m.pc_fw_low, FirmwareString(console), FirmwareString(required));
    } else {
        p.firmware.state = PreCheck::State::Fine;
        p.firmware.right = Printf(m.pc_fw_ok, FirmwareString(console));
    }

    // the key the content asks for comes with the firmware it asks for
    const int needed_gen = required ? KeyGenerationOf(required) : 0;
    const int have_gen = info.key_generation >= 0 ? info.key_generation : console ? KeyGenerationOf(console) : -1;
    if (have_gen < 0) {
        p.keys.right = m.pc_unknown;
    } else if (needed_gen > have_gen) {
        p.keys.state = PreCheck::State::Warn;
        p.keys.right = Printf(m.pc_keys_low, needed_gen, have_gen);
    } else {
        p.keys.state = PreCheck::State::Fine;
        p.keys.right = Printf(m.pc_keys_ok, std::max(needed_gen, have_gen));
    }

    if (!info.atmosphere) {
        p.atmosphere.right = m.pc_ams_none;
    } else {
        char version[24];
        std::snprintf(version, sizeof(version), "%u.%u.%u", info.ams_major, info.ams_minor, info.ams_micro);
        // the firmware the console will need to run the content: Atmosphère must support it too
        const unsigned want = required > console ? required : console;
        const unsigned target = info.ams_target & 0xFFFF0000;
        if (!target) {
            p.atmosphere.state = PreCheck::State::Fine;
            p.atmosphere.right = version;
        } else if (want > target) {
            p.atmosphere.state = PreCheck::State::Warn;
            p.atmosphere.right = Printf(m.pc_ams_low, version, FirmwareString(target), FirmwareString(want));
        } else {
            p.atmosphere.state = PreCheck::State::Fine;
            p.atmosphere.right = Printf(m.pc_ams_ok, version, FirmwareString(target)); // how far it goes
        }
    }
    return p;
}

} // namespace nxdock
