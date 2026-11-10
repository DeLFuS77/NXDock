// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <switch.h>
#include <atomic>
#include <vector>

namespace nxdock {

// How far a check goes, read from the interface while it runs.
struct VerifyProgress {
    std::atomic<u64> done{0}, total{0};      // bytes
    std::atomic<u32> parts_done{0}, parts{0}; // NCAs
    std::atomic<bool> cancel{false};
};

// What a check found, per content (the game, an update, a DLC).
struct VerifiedContent {
    NcmContentMetaKey key{};
    bool nand = false;
    u32 parts = 0;
    std::vector<NcmContentId> bad; // parts whose SHA-256 does not match, or that are missing
    u64 bad_bytes = 0;
};

// Reads every NCA of a game installed on the SD card or the internal memory and compares its
// SHA-256 with its content id (its name). Nothing is written.
Result VerifyApplication(u64 app_id, VerifyProgress& progress, std::vector<VerifiedContent>& out);

} // namespace nxdock
