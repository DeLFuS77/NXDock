// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// NXDock's stand-in for Sphaira's utils/thread.hpp: a thread on the cores the process may use,
// starting on the core asked for (the kernel moves it to an idle one of the others).
#pragma once

#include "defines.hpp"

namespace sphaira::utils {

inline u64 GetSafeCoreMask() {
    u64 process_mask{};
    if (R_FAILED(svcGetInfo(&process_mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) {
        return 0x7;
    }
    const u64 safe = process_mask & 0x7;
    return safe ? safe : process_mask;
}

inline Result CreateThread(Thread* t, ThreadFunc entry, void* arg, size_t stack_sz = 1024 * 128, int prio = 0x2D, int core = -2) {
    const u64 mask = GetSafeCoreMask();
    R_UNLESS(mask, Result_CoreUnavailable);
    if (core >= 0 && !(mask & (1ULL << core))) {
        core = -2;
    }
    R_TRY(threadCreate(t, entry, arg, nullptr, stack_sz, prio, core));
    const Result rc = svcSetThreadCoreMask(t->handle, -1, mask);
    if (R_FAILED(rc)) {
        threadClose(t);
        return rc;
    }
    R_SUCCEED();
}

} // namespace sphaira::utils
