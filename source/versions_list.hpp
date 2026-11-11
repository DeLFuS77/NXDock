// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// What is missing for the games on the console that are not in the library (6A of the visual test
// of the new tools): the list of versions of titledb (versions.txt, "id|rightsId|version" for every
// game, update and DLC it knows), downloaded at most once a day and kept on the SD card. Only
// tells: NXDock never looks for the files anywhere but the library.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace nxdock {

class VersionsList {
public:
    // The newest version of each title ID in a versions.txt; false when it is not one.
    static bool Parse(const std::string& text, std::vector<std::pair<uint64_t, uint32_t>>& out);

    void Set(std::vector<std::pair<uint64_t, uint32_t>> entries, long long made);
    bool Ready() const;
    long long Made() const; // when the list was downloaded, seconds since 1970 (0: none)
    // The newest update of a game (0: none known), and every DLC of it titledb knows.
    uint32_t LatestUpdate(uint64_t app) const;
    std::vector<uint64_t> Dlcs(uint64_t app) const;

    // The copy on the SD card, read on a thread; a new one downloaded when it is older than a day
    // and `refresh` (Settings › What is missing… on).
    void Start(bool refresh);
    ~VersionsList();

private:
    mutable std::mutex m_mutex;
    std::vector<std::pair<uint64_t, uint32_t>> m_entries; // by title ID
    long long m_made = 0;
    std::thread m_thread;
    std::atomic<bool> m_started{false}, m_stop{false};
};

} // namespace nxdock
