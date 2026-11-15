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
#include <malloc.h>
#include <switch.h>
#include <mutex>
#include <sys/stat.h>

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

// added to what the file has, unless it has grown past 2 MiB (then emptied)
extern "C" void nxdock_log_append(const char* path) {
    std::lock_guard lock(g_log_mutex);
    if (g_log) {
        std::fclose(g_log);
    }
    struct stat st{};
    const bool big = stat(path, &st) == 0 && st.st_size > (2 << 20);
    g_log = std::fopen(path, big ? "w" : "a");
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
    // NXDock's own lines and failures reach the card at once; the many lines of the core wait in the buffer
    if (std::strncmp(text, "[NXDock]", 8) == 0 || std::strncmp(text, "[http]", 6) == 0 || std::strncmp(text, "[fail]", 6) == 0) {
        std::fflush(g_log);
    }
}

extern "C" void log_write(const char* s, ...) {
    va_list v;
    va_start(v, s);
    log_write_arg(s, &v);
    va_end(v);
}

// NXDock: the memory at hand, written into the log of a job when it starts and when it ends, to
// know how much an install has as an applet and as an application with each firmware: the heap
// the loader gave NXDock and what of it is in use, and the four memory pools of the console
// (svcGetSystemInfo, which the NPDM of the loader allows).
extern "C" void nxdock_log_memory(const char* when) {
    constexpr double kMiB = 1024.0 * 1024.0;
    const AppletType type = appletGetAppletType();
    const bool applet = type != AppletType_Application && type != AppletType_SystemApplication;
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    const struct mallinfo mi = mallinfo();
    log_write("[NXDock] memory %s: %s, process %.1f of %.1f MiB, heap %.1f MiB, in use %.1f MiB (at most %.1f MiB)\n", when,
              applet ? "applet" : "application", double(used) / kMiB, double(total) / kMiB,
              double(envGetHeapOverrideSize()) / kMiB, double(mi.uordblks) / kMiB, double(mi.arena) / kMiB);
    static const char* const kPools[] = {"application", "applet", "system", "system unsafe"};
    for (u64 pool = 0; pool < 4; pool++) {
        u64 pool_total = 0, pool_used = 0;
        if (R_SUCCEEDED(svcGetSystemInfo(&pool_total, 0, INVALID_HANDLE, pool)) &&
            R_SUCCEEDED(svcGetSystemInfo(&pool_used, 1, INVALID_HANDLE, pool))) {
            log_write("[NXDock]   pool %s: %.1f of %.1f MiB used\n", kPools[pool], double(pool_used) / kMiB, double(pool_total) / kMiB);
        }
    }
}
