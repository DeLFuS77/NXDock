// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// The check of the SD card on the console: the files in sdmc:/switch/NXDock/sdcheck, written
// through the file system service, on a thread of its own, with the console kept awake.
#include "sdcheck.hpp"

#include <memory>
#include <mutex>
#include <thread>

namespace nxdock {

class SdChecker {
public:
    struct View {
        bool running = false, done = false, quick = true;
        int phase = 0;
        unsigned long long bytes_done = 0, bytes = 0;
        double write_mb = 0, read_mb = 0, elapsed = 0;
        long long free_bytes = -1, total_bytes = -1;
        SdCheckResult result;
    };

    ~SdChecker();
    bool Start(bool quick);
    void Stop();
    View Snapshot();
    // Test files a check left when the console went off: deleted when the screen opens.
    static void CleanLeftovers();
    // The free and total space of the card, for the screen before a check.
    static void Space(long long& free_bytes, long long& total_bytes);

private:
    std::thread m_thread;
    std::mutex m_mutex;
    SdCheckProgress m_progress;
    SdCheckResult m_result;
    bool m_running = false, m_done = false, m_quick = true;
    double m_started = 0;
    long long m_free = -1, m_total = -1;
};

} // namespace nxdock
