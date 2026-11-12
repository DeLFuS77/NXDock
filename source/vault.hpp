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

// The save of a game as a zip on the SD card (MTP 8A, the saves 9A): of a user (`kind` 0), of the
// console (1, device) or BCAT (2); `uid` is not read for 1 and 2. False when there is none or it
// could not be written.
bool SaveZipTo(const unsigned char uid[16], unsigned long long app_id, int kind, const std::string& zip_path);

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
        int copied = 0;      // saves copied by the last Copy all
        bool last_delete = false;
        std::vector<LibrarySave> library;
        unsigned list_version = 0;
    };

    // A copy kept on the SD card when there is no library (9A): sdmc:/switch/NXDock/saves/<title ID>/.
    struct SdCopy {
        std::string path, note, who;
        long long when = 0;
        unsigned version = 0;
        int kind = 0; // 0 a user's, 1 the console's, 2 BCAT
        bool automatic = false;
        unsigned long long size = 0;
    };
    static std::vector<SdCopy> SdCopies(unsigned long long app_id, int kind);
    // What a copy of the library is for (its account): a user's, the console's or BCAT.
    static int KindOf(const LibrarySave& copy);

    Vault();
    ~Vault();

    std::vector<User> Users();
    // The saves of a kind: 0 of `user`, 1 of the console (device), 2 BCAT.
    std::vector<Save> Saves(const User& user, int kind = 0);

    // On a thread; false while something else runs.
    bool StartList(const std::string& url, const std::string& key);
    // An empty `url` copies to the SD card (9A).
    bool StartBackup(const std::string& url, const std::string& key, const User& user, unsigned long long app_id, unsigned version,
                     const std::string& note, int kind = 0);
    bool StartRestore(const std::string& url, const std::string& key, const User& user, unsigned long long app_id,
                      const LibrarySave& copy, const std::string& before_note, int kind = 0);
    // A copy of the SD card over the save; the one it replaces goes to the SD card first.
    bool StartRestoreSd(const User& user, int kind, unsigned long long app_id, const std::string& zip, const std::string& before_note);
    // Every save given, one after the other; `automatic` copies on the SD card keep the newest three.
    struct Item {
        User user;
        int kind = 0;
        unsigned long long app_id = 0;
        unsigned version = 0;
    };
    bool StartBackupAll(const std::string& url, const std::string& key, std::vector<Item> items, const std::string& note, bool automatic);
    // A copy first (the library, or the SD card), then the save is deleted; a failed copy deletes nothing.
    bool StartDelete(const std::string& url, const std::string& key, const User& user, int kind, unsigned long long app_id, unsigned version,
                     const std::string& note);
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
