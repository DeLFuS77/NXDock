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
    std::vector<CatalogFile> files;
    InstalledState installed;

    const CatalogFile* Base() const;
    const CatalogFile* NewestUpdate() const;
    std::vector<const CatalogFile*> Dlc() const;
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

private:
    void Run(std::string url, std::string key);

    std::thread m_thread;
    std::atomic<State> m_state{State::Empty};
    std::atomic<unsigned> m_version{0};
    std::mutex m_mutex;
    std::vector<Game> m_games;
    std::string m_error;
};

// Reads the catalog JSON of the app; false when it is not one. Tested on the computer.
bool ParseCatalog(const std::string& json, std::vector<Game>& out);

// What the console has installed, by application id.
std::map<u64, InstalledState> ReadInstalled();

} // namespace nxdock
