// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// A report for the library app, sent in one go when something goes wrong: a zip with info.txt
// (what the console and NXDock are), the logs of NXDock and its history, and the newest crash
// reports of Atmosphère. Only those files are ever read: never keys, configuration, saves, user
// names or addresses of the console. The zip is made in plain C++, tested on the computer; the
// sending is on the console.
#include "console_facts.hpp"

#include <string>
#include <vector>

namespace nxdock::report {

constexpr unsigned long long kMaxZip = 16ull * 1024 * 1024;   // the most the app takes
constexpr unsigned long long kMaxLog = 4ull * 1024 * 1024;     // of a longer log, its last 4 MiB
constexpr unsigned long long kMaxCrash = 1ull * 1024 * 1024;   // of a crash report, its first 1 MiB
constexpr int kCrashReports = 5;                               // the newest ones

// A file of the report: where it is, its name in the zip, and the part of it taken.
struct File {
    std::string path, name;
    unsigned long long from = 0, size = 0;
};

// The files of the report under `sd`, the root of the SD card ("sdmc:" on the console, a folder
// on the computer):
//  - logs/: each *.log of switch/NXDock and its history.tsv, the last kMaxLog bytes of each;
//  - crash_reports/: the newest kCrashReports *.log of atmosphere/crash_reports, the first
//    kMaxCrash bytes of each (the error and the stack come first).
// Only files right in those two folders; nothing else is looked at.
std::vector<File> Files(const std::string& sd);

// info.txt: the version of NXDock and whether it is an official build, the firmware, Atmosphère,
// emuMMC, applet or application, the memory, the SD card and the date.
std::string Info(const ConsoleFacts& facts, const std::string& version);

// Writes the zip to `path`: info.txt and the files, deflated. False when it could not be written.
bool WriteZip(const std::string& path, const std::string& info, const std::vector<File>& files);

// What became of a report, for the interface to say.
struct Result {
    enum class State { Sent, NoLibrary, TooLarge, Failed };
    State state = State::Failed;
    long status = 0;            // Failed: the HTTP status of the answer, 0 when there was none
    std::string error;          // Failed without an answer: why (from curl), or "zip"
    unsigned long long bytes = 0; // the size of the zip
};

// Makes the report and posts it to the library at `url` (POST /install/report, application/zip),
// with the console key as the other requests to /install/ do. Blocks while it sends: call it off
// the interface's thread. On the console only.
Result Send(const std::string& url, const std::string& key, const ConsoleFacts& facts);

} // namespace nxdock::report
