// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The answers of yati_env.hpp, and the log of the install core.
// The emuMMC query is the one Sphaira's app.cpp makes (GPL-3.0).
#include "yati_env.hpp"
#include "log.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <switch.h>
#include <mutex>

namespace sphaira::env {
namespace {

struct AmsEmummcPaths {
    char file_based_path[0x80];
    char nintendo[0x80];
};

std::once_flag g_emummc_once;
bool g_file_based = false;

bool RunningOnAtmosphere() {
    // Atmosphere answers this exosphere query; an emulator or another firmware does not.
    u64 version = 0;
    return R_SUCCEEDED(splInitialize()) && [&] {
        const Result rc = splGetConfig(SplConfigItem(65000), &version); // ExosphereApiVersion
        splExit();
        return R_SUCCEEDED(rc) && version != 0;
    }();
}

} // namespace

bool IsAppletMode() {
    const AppletType type = appletGetAppletType();
    return type != AppletType_Application && type != AppletType_SystemApplication;
}

bool IsFileBaseEmummc() {
    std::call_once(g_emummc_once, [] {
        if (!RunningOnAtmosphere()) {
            return;
        }
        alignas(0x1000) static AmsEmummcPaths paths{};
        SecmonArgs args{};
        args.X[0] = 0xF0000404; // smcAmsGetEmunandConfig
        args.X[1] = 0;          // EXO_EMUMMC_MMC_NAND
        args.X[2] = (u64)&paths;
        svcCallSecureMonitor(&args);
        g_file_based = paths.file_based_path[0] != '\0' && paths.nintendo[0] != '\0';
        log_write("[NXDock] emummc file based: %d\n", g_file_based);
    });
    return g_file_based;
}

} // namespace sphaira::env

// --- the log of the install core: sdmc:/switch/NXDock/install.log, opened by the queue for each job.
namespace {
std::mutex g_log_mutex;
FILE* g_log = nullptr;
// each line starts with the seconds since the job began: what takes long shows at once
u64 g_log_start = 0;
bool g_log_line_start = true;
} // namespace

extern "C" void nxdock_log_open(const char* path) {
    std::lock_guard lock(g_log_mutex);
    if (g_log) {
        std::fclose(g_log);
    }
    g_log = std::fopen(path, "w");
    g_log_start = armGetSystemTick();
    g_log_line_start = true;
}

extern "C" void nxdock_log_close() {
    std::lock_guard lock(g_log_mutex);
    if (g_log) {
        std::fclose(g_log);
        g_log = nullptr;
    }
}

extern "C" void log_write_arg(const char* s, va_list* v) {
    std::lock_guard lock(g_log_mutex);
    if (!g_log) {
        return;
    }
    char text[1024];
    std::vsnprintf(text, sizeof(text), s, *v);
    const double seconds = double(armTicksToNs(armGetSystemTick() - g_log_start)) / 1e9;
    for (const char* p = text; *p;) {
        if (g_log_line_start) {
            std::fprintf(g_log, "[%7.2f] ", seconds);
        }
        const char* nl = std::strchr(p, '\n');
        const size_t n = nl ? size_t(nl - p + 1) : std::strlen(p);
        std::fwrite(p, 1, n, g_log);
        g_log_line_start = nl != nullptr;
        p += n;
    }
    std::fflush(g_log);
}

extern "C" void log_write(const char* s, ...) {
    va_list v;
    va_start(v, s);
    log_write_arg(s, &v);
    va_end(v);
}
