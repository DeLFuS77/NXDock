// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "precheck.hpp"
#include "console_facts.hpp"

#include "compress_meta.hpp"
#include "errors.hpp"
#include "net.hpp"
#include "safety.hpp"
#include "uninstall.hpp"

#include <switch.h>

#include <cstdio>
#include <cstring>
#include <ctime>

namespace nxdock {

SystemInfo ReadSystemInfo() {
    SystemInfo info;
    const u32 hos = hosversionGet();
    info.firmware = (HOSVER_MAJOR(hos) << 26) | (HOSVER_MINOR(hos) << 20) | (HOSVER_MICRO(hos) << 16);
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw{};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
            info.firmware_text.assign(fw.display_version, strnlen(fw.display_version, sizeof(fw.display_version)));
        }
        setsysExit();
    }
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

ConsoleFacts ReadConsoleFacts(const SystemInfo& sys) {
    ConsoleFacts f;
    f.firmware = !sys.firmware_text.empty() ? sys.firmware_text : sys.firmware ? FirmwareString(sys.firmware) : std::string();
    if (sys.atmosphere) {
        char version[24];
        std::snprintf(version, sizeof(version), "%u.%u.%u", sys.ams_major, sys.ams_minor, sys.ams_micro);
        f.atmosphere = version;
    }
    const Safety::Answer emummc = ReadEmuMmc();
    f.emummc = emummc == Safety::Answer::Yes ? 1 : emummc == Safety::Answer::No ? 0 : -1;
    // the free space as the top bar reads it: the file system of the SD card, ncm for the internal memory
    s64 free_bytes = -1, total_bytes = -1;
    SdCardSpace(free_bytes, total_bytes);
    f.sd_free = free_bytes;
    f.sd_total = total_bytes;
    StorageSpace(NcmStorageId_BuiltInUser, free_bytes, total_bytes);
    f.nand_free = free_bytes;
    f.nand_total = total_bytes;
    f.sigpatches = FindSigpatches("sdmc:").how;
    f.applet = net::IsAppletMode();
    u64 v = 0;
    if (R_SUCCEEDED(svcGetInfo(&v, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0))) f.mem_total = (long long)v;
    if (R_SUCCEEDED(svcGetInfo(&v, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0))) f.mem_used = (long long)v;
    f.when = (long long)std::time(nullptr);
    return f;
}

} // namespace nxdock
