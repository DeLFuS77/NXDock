// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "usb30.hpp"

#include "log.hpp"
#include "usb30_ini.hpp"

#include <switch.h>
#include <sys/stat.h>

#include <cstdio>
#include <string>

namespace nxdock {

namespace {

constexpr const char* kIni = "sdmc:/atmosphere/config/system_settings.ini";
constexpr const char* kBak = "sdmc:/atmosphere/config/system_settings.ini.nxdock-bak";
constexpr const char* kTmp = "sdmc:/atmosphere/config/system_settings.ini.nxdock-tmp";

bool ReadAll(const char* path, std::string& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

bool WriteAll(const char* path, const std::string& bytes) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool wrote = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    const bool closed = std::fclose(f) == 0;
    if (!wrote || !closed) {
        std::remove(path);
        return false;
    }
    return true;
}

} // namespace

Usb30State ReadUsb30() {
    Usb30State s;
    if (R_SUCCEEDED(setsysInitialize())) {
        u8 value = 0;
        u64 size = 0;
        if (R_SUCCEEDED(setsysGetSettingsItemValue("usb", "usb30_force_enabled", &value, sizeof(value), &size)) && size >= 1) {
            s.active = value ? 1 : 0;
        }
        setsysExit();
    }
    std::string ini;
    if (ReadAll(kIni, ini)) s.file = Usb30InIni(ini);
    return s;
}

int UsbSpeedNow() {
    UsbState state = UsbState_Detached;
    if (R_FAILED(usbDsGetState(&state)) || state != UsbState_Configured) return 0;
    UsbDeviceSpeed speed = UsbDeviceSpeed_None;
    if (R_FAILED(usbDsGetSpeed(&speed))) return 0;
    if (speed == UsbDeviceSpeed_Super) return 3;
    return speed == UsbDeviceSpeed_None ? 0 : 2;
}

bool WriteUsb30(bool on, bool& backed) {
    backed = false;
    mkdir("sdmc:/atmosphere", 0777);
    mkdir("sdmc:/atmosphere/config", 0777);
    std::string ini;
    const bool had = ReadAll(kIni, ini);
    if (!had) {
        // missing, or there and unreadable: an unreadable one is never written over
        FILE* f = std::fopen(kIni, "rb");
        if (f) {
            std::fclose(f);
            return false;
        }
        ini.clear();
    }
    const std::string next = SetUsb30InIni(ini, on);
    if (had && next == ini) return true; // as asked already
    if (had) {
        if (!WriteAll(kBak, ini)) return false; // no copy, no change
        backed = true;
    }
    if (!WriteAll(kTmp, next)) return false;
    // FAT32 does not rename over a file: the old one goes first (its copy is kept)
    if (had && std::remove(kIni) != 0) {
        std::remove(kTmp);
        return false;
    }
    if (std::rename(kTmp, kIni) != 0) {
        // put back what was there
        if (had) WriteAll(kIni, ini);
        std::remove(kTmp);
        return false;
    }
    log_write("[NXDock] usb30_force_enabled set to %d in system_settings.ini\n", on ? 1 : 0);
    return true;
}

bool RebootConsole() {
    if (R_FAILED(spsmInitialize())) return false;
    const Result rc = spsmShutdown(true);
    spsmExit();
    return R_SUCCEEDED(rc);
}

} // namespace nxdock
