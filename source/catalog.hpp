// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace nxdock {

using u64 = std::uint64_t;
using u32 = std::uint32_t;

// A file of the library app: the game, an update or a DLC.
struct CatalogFile {
    enum class Kind { Game, Update, Dlc };
    std::string id, name, dlc_name, format, url;
    Kind kind = Kind::Game;
    u64 title_id = 0;
    u32 version = 0;
    u64 size = 0;
    u32 required_system = 0;
};

// What the console has of a game now.
struct InstalledState {
    bool base = false;
    u32 patch = 0; // version of the update installed, 0 without one
    std::set<u64> dlc;
    bool base_nand = false; // the game is in the internal memory (an update goes there too)
};

// A game of the library with its files, and what the console has of it.
struct Game {
    std::string key, name, cover; // cover: the address of its picture in the app
    u64 title_id = 0;
    long long added = 0; // when its newest file came to the library, seconds since 1970 (0: unknown)
    std::vector<CatalogFile> files;
    InstalledState installed;

    const CatalogFile* Base() const;
    const CatalogFile* NewestUpdate() const;
    std::vector<const CatalogFile*> Dlc() const;
    // how many DLC files, without making the list (called for every game when counting)
    size_t DlcCount() const;
    bool UpdateAvailable() const;
    bool MissingDlc() const;
    u64 SizeOfAll() const;
};

// The catalog of the library and what is installed, read on a thread of its own.
class Catalog {
public:
    enum class State { Empty, Loading, Ready, Failed };

    ~Catalog();
    void Load(const std::string& url, const std::string& key);
    State GetState() const { return m_state.load(); }
    std::string Error();
    // A copy of the games, refreshed when the load ends. Cheap to call: a version number
    // says when to take a new copy.
    unsigned Version() const { return m_version.load(); }
    std::vector<Game> Games();
    // The same, without copying on the caller's thread: the copy of the newest load, made on the
    // load's thread, handed over once; later calls copy as Games().
    std::vector<Game> TakeGames();
    // Stops a load that waits on the library; what was shown stays.
    void Cancel() { m_cancel = true; }
    // The library is still preparing its catalog (a large one, the first time): how far it is.
    bool Preparing(int& done, int& total) const {
        done = m_done.load();
        total = m_total.load();
        return m_preparing.load();
    }
    // The games shown are the list kept on the SD card, the library did not give a new one.
    bool Stale() const { return m_stale.load(); }

private:
    void Run(std::string url, std::string key);
    void Publish(std::vector<Game> games);

    std::thread m_thread;
    std::atomic<bool> m_cancel{false}, m_preparing{false}, m_stale{false};
    std::atomic<int> m_done{0}, m_total{0};
    std::atomic<State> m_state{State::Empty};
    std::atomic<unsigned> m_version{0};
    std::mutex m_mutex;
    std::vector<Game> m_games;
    std::vector<Game> m_games_out; // a copy of m_games for TakeGames
    bool m_out_ready = false;
    std::string m_error;
};

// Reads the catalog JSON of the app; false when it is not one. Tested on the computer.
bool ParseCatalog(const std::string& json, std::vector<Game>& out);

// A file the library sends from the web ("Send to console"); the console asks before installing.
struct PushItem {
    std::string id, file_id, url, game, kind, format, name, from;
    u64 title_id = 0;
    u32 version = 0;
    u64 size = 0;
};
// Reads the answer of /install/push/wait; false when it is not one. Tested on the computer.
bool ParsePush(const std::string& json, std::vector<PushItem>& out);

// One content on the console, for the inventory shared with the app (only when the user agrees).
struct InstalledContent {
    u64 title_id = 0;
    unsigned char type = 0; // NcmContentMetaType: 0x80 game, 0x81 update, 0x82 DLC
    u32 version = 0;
    bool nand = false;
};
// The body of /install/inventory. Tested on the computer.
std::string InventoryJson(const std::vector<InstalledContent>& items);

// What the console has installed, by application id; with list, every content too.
std::map<u64, InstalledState> ReadInstalled(std::vector<InstalledContent>* list = nullptr);

} // namespace nxdock
