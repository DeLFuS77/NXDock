// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nxdock {

// One line of the install history, kept on the SD card in sdmc:/switch/NXDock/history.tsv.
struct HistoryEntry {
    enum class Result { Installed, Failed, Cancelled, Removed };

    int64_t when = 0;      // seconds since 1970, the console's clock
    std::string game, label, format;
    bool nand = false;     // to the internal memory
    uint64_t size = 0;     // bytes of the file in the library
    uint64_t written = 0;  // bytes the install wrote
    double seconds = 0;    // how long it took
    Result result = Result::Installed;
    std::string error;     // why it failed, as it was shown

    // MB/s over the network: the file of the library over the time it took.
    double Speed() const { return seconds > 0.5 ? double(size) / seconds / (1024.0 * 1024.0) : 0; }
};

// The history file: one entry per line, tab-separated, the newest last. Kept to the newest
// kKeep entries; written whole to a new file and put in place, so a power cut never leaves half.
class History {
public:
    static constexpr size_t kKeep = 300;

    explicit History(std::string path) : m_path(std::move(path)) {}

    // The entries, the newest first.
    std::vector<HistoryEntry> Load() const;
    bool Append(const HistoryEntry& entry) const;
    bool Clear() const;

    // One line of the file, and back (false when the line is not one).
    static std::string Format(const HistoryEntry& entry);
    static bool Parse(const std::string& line, HistoryEntry& out);

private:
    std::string m_path;
};

// sdmc:/switch/NXDock/history.tsv
const char* HistoryPath();

} // namespace nxdock
