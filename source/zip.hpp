// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace nxdock::zip {

// A small zip writer: files stored as they are (saves are small and often packed already),
// written straight to a file. Sizes up to 4 GB each.
class Writer {
public:
    ~Writer();
    bool Open(const std::string& path);
    // A folder ("data/"), or a file read with `read(offset, buffer, size)` (false on error):
    // it is read twice, once for its CRC-32 and once to be written.
    bool AddFolder(const std::string& name);
    bool AddFile(const std::string& name, uint64_t size, const std::function<bool(uint64_t, void*, size_t)>& read);
    bool Close();

private:
    struct Entry {
        std::string name;
        uint32_t crc = 0, size = 0;
        uint32_t offset = 0;
    };
    bool Put(const void* data, size_t size);
    FILE* m_file = nullptr;
    uint32_t m_pos = 0;
    std::vector<Entry> m_entries;
    bool m_ok = true;
};

// A zip reader for files stored or deflated (what NXDock writes, and most mod packages).
struct Entry {
    std::string name;
    uint16_t method = 0;   // 0 stored, 8 deflated
    uint32_t crc = 0;
    uint64_t packed = 0, size = 0;
    uint64_t local = 0;    // where its local header is
    bool Folder() const { return !name.empty() && name.back() == '/'; }
};

class Reader {
public:
    ~Reader();
    bool Open(const std::string& path);
    const std::vector<Entry>& Entries() const { return m_entries; }
    // Gives the bytes of an entry in pieces to `out` (false stops); checks the CRC-32.
    bool Extract(const Entry& e, const std::function<bool(const void*, size_t)>& out);

private:
    FILE* m_file = nullptr;
    std::vector<Entry> m_entries;
};

// A name inside a zip that is safe to write: no absolute path, no "..", no drive or backslash.
bool SafeName(const std::string& name);

} // namespace nxdock::zip
