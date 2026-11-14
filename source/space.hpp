// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
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
    long long play_minutes = -1; // how long it was played, all told (-1: unknown)
    int launches = -1;           // how many times it was opened (-1: unknown)
    struct Content {
        unsigned long long id = 0;
        unsigned version = 0;
        unsigned char type = 0; // NcmContentMetaType
        bool nand = false;
        unsigned long long bytes = 0;
        unsigned required = 0; // a DLC: the version of its game it needs (0: any)
    };
    std::vector<Content> contents;
    unsigned long long Bytes() const { return sd_bytes + nand_bytes; }
};

// An installed content as an NSP made as it is read (the dumps, and the Installed games folder of
// MTP, 8A of the new tools): its size and a reader. Null when it cannot be read. ncm must be open.
struct InstalledNsp {
    unsigned long long size = 0;
    std::function<bool(unsigned long long, void*, size_t)> read;
};
std::shared_ptr<InstalledNsp> OpenInstalledNsp(unsigned long long id, unsigned version, unsigned char type, bool nand);

// The game card (14A of the visual test of groups 3 and 4): whether one is in, what it holds (its
// game, and the update and DLC it brings, with their sizes) and its name; and its secure
// partition as an NSP made as it is read, to install from it. Null without a card.
bool GameCardInserted();
struct GameCard {
    bool ok = false;            // read
    unsigned long long app = 0; // its game
    std::string name;
    std::vector<InstalledApp::Content> contents; // the game first, then its update, then its DLC
    unsigned long long Bytes() const;
};
GameCard ReadGameCard();
std::shared_ptr<InstalledNsp> OpenGameCardNsp();

// What "Clean leftovers" would remove: contents no title refers to, and tickets of titles that
// are not on the console in any way (archived games keep theirs).
struct Leftovers {
    int contents = 0, tickets = 0;
    int placeholders = 0; // unfinished installs of other installers (24A); never those NXDock can go on with
    int addons = 0;       // updates and DLC whose game is not on the console, nor in its records
    int personal = 0;     // of the tickets, those personal (bought with an account); saved to the SD card first
    unsigned long long bytes = 0; // all of it
    bool Any() const { return contents || tickets || placeholders || addons; }
};

// A ticket of the console (6A): its rights id, and whether it is personal (else common).
struct TicketId {
    unsigned char rights[16] = {};
    bool personal = false;
};
// Where the tickets are saved before they are deleted: sdmc:/switch/NXDock/tickets/<rights id>.tik
// (and .cert when the console gives its certificate).
constexpr const char* kTicketsDir = "sdmc:/switch/NXDock/tickets";
// Every ticket of the console, common and personal; false when it could not be read.
bool ListTickets(std::vector<TicketId>& out);
// The tickets of a game, its updates and its DLC (7A).
std::vector<TicketId> TicketsOf(unsigned long long app);
// Saves each ticket to kTicketsDir, then deletes those saved; one whose copy fails stays. How many
// went; `kept` says how many stayed.
int SaveAndDeleteTickets(const std::vector<TicketId>& tickets, int* kept = nullptr);
// The games the console has a record of (installed or archived); empty when ns does not answer.
std::set<unsigned long long> AppRecords();

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
    enum class Task { None, Scan, Clean, Archive, Move, Dump, Remove, Tickets };
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
        int tickets_all = -1; // every ticket of the console, at the last scan (6A)
        int tickets_done = 0, tickets_kept = 0; // the last Tickets task: deleted, and kept as their copy failed
    };

    ~Space();
    bool Busy();
    View Snapshot();

    bool StartScan();
    bool StartClean();
    bool StartArchive(std::vector<unsigned long long> apps);
    bool StartMove(unsigned long long app, bool to_nand);
    // Takes contents of a game off the console (In the console, of the Installed tab): its updates
    // and DLC first, the game last; the saves stay. The game too means all of it goes.
    // `before`, when given, runs before a game itself goes (its saves copied, 3A); an error stops there.
    // `after`, when given, runs once a game itself is gone (its mods and its tickets, 7A).
    bool StartRemove(unsigned long long app, std::vector<InstalledApp::Content> items,
                     std::function<std::string(unsigned long long)> before = {}, std::function<void(unsigned long long)> after = {});
    // Several games (In the console, marked with X): each with what of it goes.
    bool StartRemove(std::vector<std::pair<unsigned long long, std::vector<InstalledApp::Content>>> games,
                     std::function<std::string(unsigned long long)> before = {}, std::function<void(unsigned long long)> after = {});
    // Deletes tickets (6A), each saved to the SD card first; one whose copy fails stays.
    bool StartDeleteTickets(std::vector<TicketId> tickets);
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
