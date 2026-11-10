// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nxdock {

// A copy of a save kept in the library app (GET /install/saves).
struct LibrarySave {
    std::string title_id, game, id, note, account, console;
    long long when = 0;
    unsigned version = 0;
    unsigned long long size = 0;
    bool pinned = false;
};

// The answer of GET /install/saves, newest first. Tested on the computer.
std::vector<LibrarySave> ParseSaves(const std::string& json);
// "0100000000010000" from a title id, and back.
std::string TitleHex(unsigned long long id);

// The saves of every user of the console for a game, copied to the library with a note, before
// a change to the game (18A): an update, a repair, a removal or going back to an older version.
// On the caller's thread. Empty when it went well or there was nothing to copy; else the error,
// packed (errors.hpp). `current` is the version installed now, kept with the copy.
std::string CopySavesBefore(const std::string& url, const std::string& key, unsigned long long app_id, unsigned current,
                            const std::string& note, const std::function<void(unsigned long long, unsigned long long)>& progress);

// The vault of saves (6A): the saves of a user of the console, copied to the library app as a
// zip with a note, and a copy brought back. Restoring always asks first and sends a copy of the
// save it replaces to the library before.
class Vault {
public:
    struct User {
        unsigned char uid[16];
        std::string name;
    };
    struct Save {
        unsigned long long app_id = 0;
        unsigned long long size = 0;
        std::string name; // the game, from the console
    };
    enum class Stage { Idle, Reading, Copying, Sending, Receiving, Restoring };
    struct View {
        Stage stage = Stage::Idle;
        unsigned long long done = 0, total = 0;
        std::string error;   // packed (errors.hpp); empty when it went well
        int finished = 0;    // counts the ends: the screen notices a new one
        bool last_restore = false;
        bool listed = false; // the copies of the library were read
        std::vector<LibrarySave> library;
        unsigned list_version = 0;
    };

    Vault();
    ~Vault();

    std::vector<User> Users();
    std::vector<Save> Saves(const User& user);

    // On a thread; false while something else runs.
    bool StartList(const std::string& url, const std::string& key);
    bool StartBackup(const std::string& url, const std::string& key, const User& user, unsigned long long app_id, unsigned version,
                     const std::string& note);
    bool StartRestore(const std::string& url, const std::string& key, const User& user, unsigned long long app_id,
                      const LibrarySave& copy, const std::string& before_note);
    View Snapshot();
    bool Busy();

private:
    void Run(std::function<void()> work);
    void Done(const std::string& error, bool restore = false);
    void Progress(unsigned long long done, unsigned long long total, Stage stage);
    std::thread m_thread;
    std::mutex m_mutex;
    View m_view;
    bool m_busy = false;
};

} // namespace nxdock
