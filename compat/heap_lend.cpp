// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "heap_lend.hpp"
#include "log.hpp"

#include <cstdlib>
#include <mutex>
#include <switch.h>

namespace nxdock::heap {
namespace {

constexpr size_t kLarge = 16u << 20;     // only a block this big is looked for past the heap
constexpr u64 kKeep = 2u << 20;          // what is always left to the process
constexpr u64 kHeapAlign = 0x200000;     // svcSetHeapSize takes multiples of 2 MiB

std::mutex g_mutex;
void* g_lent = nullptr; // the block past the heap, while zstd holds it
u64 g_heap_size = 0;    // the size of the heap before it grew

constexpr double MiB(u64 bytes) {
    return double(bytes) / double(1u << 20);
}

void* Lend(size_t size) {
    // only when the loader gave the heap (an NRO): its end is the end of the process heap
    if (!envHasHeapOverride()) return nullptr;
    u64 total = 0, used = 0, region = 0;
    if (R_FAILED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)) ||
        R_FAILED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0)) ||
        R_FAILED(svcGetInfo(&region, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0))) {
        return nullptr;
    }
    const u64 end = u64(envGetHeapOverrideAddr()) + envGetHeapOverrideSize();
    if (end <= region || (end - region) % kHeapAlign != 0) return nullptr;
    const u64 current = end - region;
    const u64 grow = (u64(size) + kHeapAlign - 1) & ~(kHeapAlign - 1);
    if (total < used + grow + kKeep) {
        log_write("[NXDock] memory: %.1f MiB asked, %.1f MiB left to the process\n", MiB(size), MiB(total - used));
        return nullptr;
    }
    void* heap = nullptr;
    const Result rc = svcSetHeapSize(&heap, current + grow);
    if (R_FAILED(rc) || u64(heap) != region) {
        log_write("[NXDock] memory: the heap did not grow by %.1f MiB (0x%X)\n", MiB(grow), rc);
        if (R_SUCCEEDED(rc)) svcSetHeapSize(&heap, current);
        return nullptr;
    }
    g_heap_size = current;
    g_lent = reinterpret_cast<void*>(end);
    log_write("[NXDock] memory: the heap grew by %.1f MiB for a block of %.1f MiB\n", MiB(grow), MiB(size));
    return g_lent;
}

} // namespace

void* Alloc(void*, size_t size) {
    if (void* p = std::malloc(size); p || size < kLarge) return p;
    std::lock_guard lock(g_mutex);
    return g_lent ? nullptr : Lend(size);
}

void Free(void*, void* address) {
    if (!address) return;
    {
        std::lock_guard lock(g_mutex);
        if (address == g_lent) {
            void* heap = nullptr;
            const Result rc = svcSetHeapSize(&heap, g_heap_size);
            log_write("[NXDock] memory: the heap is back to its size (0x%X)\n", rc);
            g_lent = nullptr;
            return;
        }
    }
    std::free(address);
}

} // namespace nxdock::heap
