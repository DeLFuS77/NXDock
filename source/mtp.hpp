// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "install_queue.hpp"
#include "yati/source/base.hpp"

#include <functional>
#include <memory>
#include <string>

namespace sphaira::ui {
class ProgressBox;
}

namespace nxdock {

// Installing by dragging files from the computer (MTP, with libhaze). The console shows three
// places in the file explorer of the computer:
//  - "Install to the SD card": a game file dropped there is installed as it arrives, nothing is
//    kept on the SD card (a stream install: all or nothing as always);
//  - "Install to the internal memory": the same, only after the console said yes (X on the
//    MTP screen, with its question);
//  - "SD card": the card itself, to copy files.
struct MtpView {
    bool running = false;
    bool connected = false;
    std::string file;      // the game arriving, if any
    u64 received = 0, size = 0;
    bool nand_allowed = false;
    int done = 0, failed = 0;
    std::string last_error; // packed (errors.hpp)
};

// `enqueue` puts the job of a file that starts to arrive in the install queue; it returns
// false when the queue is busy (the computer is told to wait and tries again).
bool MtpStart(std::function<bool(InstallJob)> enqueue, std::function<bool()> queue_busy);
void MtpStop();
MtpView MtpSnapshot();
void MtpAllowNand(bool on);
// Called by the queue when an MTP job ends, to count it and free the next one.
void MtpJobEnded(bool ok, const std::string& error);

// The stream of the file arriving now, for the install queue (null when none).
std::unique_ptr<sphaira::yati::source::Base> MtpStreamSource(sphaira::ui::ProgressBox* box);

} // namespace nxdock
