// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nxdock {

// Space and dumps (8A of the visual test of phases 9 to 15). Everything that writes runs on a
// thread of its own and is asked for first on the screen; the internal memory always asks.
struct InstalledApp {
    unsigned long long app_id = 0;
    std::string name;
    unsigned long long sd_bytes = 0, nand_bytes = 0;
    bool archived = false; // a record without content: archived from the HOME menu or here
    long long idle_minutes = -1; // since it was last played (-1: never, or unknown)
    struct Content {
        unsigned long long id = 0;
        unsigned version = 0;
        unsigned char type = 0; // NcmContentMetaType
        bool nand = false;
        unsigned long long bytes = 0;
    };
    std::vector<Content> contents;
    unsigned long long Bytes() const { return sd_bytes + nand_bytes; }
};

// What "Clean leftovers" would remove: contents no title refers to, and tickets of titles that
// are not on the console in any way (archived games keep theirs).
struct Leftovers {
    int contents = 0, tickets = 0;
    int placeholders = 0; // unfinished installs of other installers (24A); never those NXDock can go on with
    int addons = 0;       // updates and DLC whose game is not on the console, nor in its records
    unsigned long long bytes = 0; // all of it
    bool Any() const { return contents || tickets || placeholders || addons; }
};

// The free space of the SD card and the internal memory, and their size.
struct Storage {
    long long sd_free = -1, sd_total = -1, nand_free = -1, nand_total = -1;
};

// The games that free at least `needed` bytes of the SD card when archived: the ones played least
// lately first (never played before all), the largest first among equals. The suggestion of the
// planner ("this queue needs X GB: archive these"). Tested on the computer.
std::vector<int> PlanArchive(const std::vector<InstalledApp>& apps, unsigned long long needed, bool nand);

class Space {
public:
    enum class Task { None, Scan, Clean, Archive, Move, Dump };
    struct View {
        Task task = Task::None;
        bool scanned = false;
        std::vector<InstalledApp> apps;
        Leftovers leftovers;
        Storage storage;
        unsigned long long done = 0, total = 0;
        std::string current;  // what is being worked on
        std::string error;    // packed; empty when it went well
        int finished = 0;
        Task last = Task::None;
        bool card = false;    // a game card is in
        int names_done = 0, names_total = 0; // the names read after the scan (NameLater)
        std::string card_name;
        unsigned long long card_bytes = 0;
    };

    ~Space();
    bool Busy();
    View Snapshot();

    bool StartScan();
    bool StartClean();
    bool StartArchive(std::vector<unsigned long long> apps);
    bool StartMove(unsigned long long app, bool to_nand);
    // A dump to the library app (PUT /install/upload/{name}): an installed content, or the game
    // card (its secure partition), as an NSP.
    bool StartDump(const std::string& url, const std::string& key, unsigned long long content_id, unsigned version, unsigned char type,
                   bool nand, const std::string& name);
    bool StartCardDump(const std::string& url, const std::string& key, const std::string& name);
    void Cancel() { m_cancel = true; }

private:
    void Run(Task task, std::function<std::string()> work);
    void Progress(unsigned long long done, unsigned long long total, const std::string& current = {});
    void ScanNow();
    // The names of the games, read after the scan on a thread of their own, and kept: with many
    // games, reading each name takes long, and the screen answers meanwhile.
    void NameLater();
    std::map<unsigned long long, std::string> m_names;
    std::thread m_name_thread;
    std::atomic<bool> m_naming{false};
    std::thread m_thread;
    std::mutex m_mutex;
    View m_view;
    bool m_busy = false;
    std::atomic<bool> m_cancel{false};
};

} // namespace nxdock
