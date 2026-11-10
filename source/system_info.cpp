// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "precheck.hpp"

#include <switch.h>

namespace nxdock {

SystemInfo ReadSystemInfo() {
    SystemInfo info;
    const u32 hos = hosversionGet();
    info.firmware = (HOSVER_MAJOR(hos) << 26) | (HOSVER_MINOR(hos) << 20) | (HOSVER_MICRO(hos) << 16);
    if (R_SUCCEEDED(splInitialize())) {
        // exosphere's API information: the version of Atmosphère, the newest master key of the
        // console and the newest firmware this Atmosphère supports
        u64 api = 0;
        if (R_SUCCEEDED(splGetConfig(SplConfigItem(65000), &api))) {
            info.atmosphere = true;
            info.ams_major = unsigned(api >> 56) & 0xFF;
            info.ams_minor = unsigned(api >> 48) & 0xFF;
            info.ams_micro = unsigned(api >> 40) & 0xFF;
            info.key_generation = int((api >> 32) & 0xFF);
            const u32 target = u32(api & 0xFFFFFFFF); // major << 24 | minor << 16 | micro << 8
            info.ams_target = ((target >> 24) & 0xFF) << 26 | ((target >> 16) & 0xFF) << 20 | ((target >> 8) & 0xFF) << 16;
        }
        splExit();
    }
    return info;
}

} // namespace nxdock
