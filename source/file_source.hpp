// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "yati/source/base.hpp"

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace nxdock {

// A game file on the SD card or on a USB disk, for the install core. A split file (the parts
// 00, 01… of a folder, or name.nsp.00, .01…) is read as one: the parts one after the other.
class FileSource final : public sphaira::yati::source::Base {
public:
    explicit FileSource(std::vector<std::string> parts);
    ~FileSource() override;

    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override;
    s64 Size() const { return m_size; }

private:
    struct Part {
        std::string path;
        FILE* file = nullptr;
        s64 start = 0, size = 0;
    };
    std::vector<Part> m_parts;
    std::vector<std::vector<char>> m_buffers;
    s64 m_size = 0;
    std::mutex m_mutex;
};

// One entry of a folder as the file list shows it (4A): a folder, or a game file, split or not.
struct LocalEntry {
    std::string name;               // as shown: a split file by its whole name
    std::string path;               // the folder, or the file (the first part)
    std::vector<std::string> parts; // a game file: its parts in order (one for a whole file)
    bool dir = false;
    unsigned long long size = 0;    // all the parts
    std::string format;             // nsp, nsz, xci, xcz
};

// The folders and game files of a folder, folders first and then by name; the rest is left out,
// unless `all`: then every file too (a file that is not a game has no format), save the hidden ones
// and what computers leave (._*, Thumbs.db…). `root` is the place itself (sdmc:/, ums0:/…): ".." is
// not given there.
std::vector<LocalEntry> ListFolder(const std::string& folder, bool root, bool all = false);

// The game format of a name (nsp, nsz, xci, xcz), or empty.
std::string GameFormat(const std::string& name);

} // namespace nxdock
