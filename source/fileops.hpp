// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// What the file list does with files and folders (1A to 3A of the visual test of the new tools,
// 11/11): delete, copy, move, rename, a new folder, the size of a folder, and the folders NXDock
// never touches. Plain files only (the SD card and USB disks); the internal memory is never shown.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace nxdock::fileops {

// What may be done with a path. Copying from anywhere is fine; deleting, renaming or moving is
// refused for System and Self, and warned of (in amber) for Careful.
enum class Guard {
    Free,
    Careful, // inside atmosphere/: allowed, with a warning
    System,  // the root, Nintendo/, emuMMC/, bootloader/, atmosphere/, switch/, config/ themselves
    Self,    // NXDock itself: its folder, its .nro and its settings
};
Guard GuardOf(const std::string& path);
// The worst guard of several paths.
Guard GuardOfAll(const std::vector<std::string>& paths);

std::string Join(const std::string& folder, const std::string& name);
std::string Parent(const std::string& path);   // "sdmc:/a/b" -> "sdmc:/a/"
std::string NameOf(const std::string& path);   // "sdmc:/a/b/" -> "b"
std::string DeviceOf(const std::string& path); // "sdmc:/a" -> "sdmc:"

// Files a computer leaves that are no use here (._*, .Trashes, $RECYCLE.BIN, Thumbs.db…).
bool Junk(const std::string& name);
// A name that can be given to a file: not empty, no / \ : * ? " < > |, not "." or "..".
bool ValidName(const std::string& name);

// How many files a path holds, and their bytes (a file: itself).
struct Count {
    unsigned long long files = 0, bytes = 0, folders = 0;
};
Count CountTree(const std::string& path);
Count CountAll(const std::vector<std::string>& paths);

// The work of a copy, a move or a delete, read by the screen while it runs on its own thread.
struct Progress {
    std::atomic<unsigned long long> bytes_done{0}, bytes_total{0};
    std::atomic<unsigned long long> files_done{0}, files_total{0};
    std::atomic<bool> cancel{false}, done{false}, failed{false};
    std::mutex mutex;
    std::string current;   // the file at work, as it is shown
    std::string error;     // what failed: the path
    std::vector<std::string> made; // copy: what was made at the destination (names as made)
    void SetCurrent(const std::string& s) {
        std::lock_guard lock(mutex);
        current = s;
    }
    std::string Current() {
        std::lock_guard lock(mutex);
        return current;
    }
};

// Deletes files and folders with what they hold. Stops when asked; what is deleted stays deleted.
bool DeleteAll(const std::vector<std::string>& paths, Progress& p);

// Copies (or moves) files and folders into `folder`. A name that is there already gets " (2)",
// " (3)"… so nothing is written over. A move on the same device is a rename; between devices it is a
// copy, read back by size, then the original goes. `split_big`: the destination cannot hold files of
// 4 GB or more (a FAT32 USB disk): such a file becomes a folder of parts 00, 01… as the console and
// NXDock read them. Stopping leaves whole files where they are and deletes the one half written.
// `max_file` and `part` are for the tests.
bool CopyAll(const std::vector<std::string>& paths, const std::string& folder, bool move, bool split_big, Progress& p,
             unsigned long long max_file = 0xFFFFFFFFull, unsigned long long part = 0xFFFF0000ull);

// Whether `inside` is `folder` itself or under it (a folder pasted into itself).
bool Within(const std::string& inside, const std::string& folder);

// A name free in `folder`: `name`, else "name (2).ext", "name (3).ext"…
std::string FreeName(const std::string& folder, const std::string& name);

// A new empty folder; false when it is there already or cannot be made.
bool MakeFolder(const std::string& folder, const std::string& name);
// A new name in the same folder; false when that name is taken or it fails.
bool Rename(const std::string& path, const std::string& name);

// Every game file (nsp, nsz, xci, xcz, split ones as one) under the paths, folders gone through.
struct GameFile {
    std::string name;               // as shown
    std::vector<std::string> parts; // in order
    unsigned long long size = 0;
    std::string format;
};
std::vector<GameFile> GamesUnder(const std::vector<std::string>& paths, int max_depth = 6);

// The largest file a FAT32 file system holds, and the size of each part of a split file.
inline constexpr unsigned long long kFat32Max = 0xFFFFFFFFull;
inline constexpr unsigned long long kSplitPart = 0xFFFF0000ull;

} // namespace nxdock::fileops
