// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <thread>

namespace nxdock {

// Whether installing here is risky, read once in the background when NXDock opens:
//  - emuMMC: Atmosphère says whether the system runs from an emuMMC (a copy of the internal
//    memory on the SD card) or from the real one (sysMMC);
//  - 90DNS or a DNS block: the names of Nintendo's servers do not lead to Nintendo.
// Before the first install of a session NXDock warns when either is missing.
class Safety {
public:
    enum class Answer { Unknown, Yes, No };

    Safety();
    ~Safety();
    bool Done() const { return m_done.load(); }
    Answer EmuMmc() const { return m_emummc.load(); }
    Answer NintendoBlocked() const { return m_blocked.load(); }
    // A warning is due: not on an emuMMC, or Nintendo reachable.
    bool Risky() const { return Done() && (EmuMmc() != Answer::Yes || NintendoBlocked() == Answer::No); }

private:
    void Run();
    std::thread m_thread;
    std::atomic<bool> m_done{false};
    std::atomic<Answer> m_emummc{Answer::Unknown}, m_blocked{Answer::Unknown};
};

// Whether an address of a Nintendo server, as the DNS of the console answered it, leads
// nowhere (a block, or 90DNS). Tested on the computer.
bool IsBlockedAddress(unsigned char a, unsigned char b, unsigned char c, unsigned char d);

} // namespace nxdock
