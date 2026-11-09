// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// NXDock's stand-in for the parts of Sphaira's fs.hpp the yati core uses: a path type, and the
// file system of an NCA (opened by its path) to read the content meta from.
#pragma once

#include "defines.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

namespace sphaira::fs {

struct FsPath {
    FsPath() = default;
    FsPath(const char* p) { std::snprintf(s, sizeof(s), "%s", p ? p : ""); }
    FsPath& operator=(const char* p) {
        std::snprintf(s, sizeof(s), "%s", p ? p : "");
        return *this;
    }
    operator char*() { return s; }
    operator const char*() const { return s; }

    char s[FS_MAX_PATH]{};
};

inline FsPath AppendPath(const char* dir, const char* name) {
    FsPath out;
    const size_t n = std::strlen(dir);
    const bool slash = n && dir[n - 1] == '/';
    // a path longer than FS_MAX_PATH is cut; the open that follows then fails
    if (std::snprintf(out.s, sizeof(out.s), slash ? "%s%s" : "%s/%s", dir, name) >= int(sizeof(out.s))) {
        out.s[sizeof(out.s) - 1] = 0;
    }
    return out;
}

struct File {
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() {
        if (m_open) fsFileClose(&m_file);
    }
    Result Read(s64 off, void* buf, u64 size, u32 option, u64* bytes_read) {
        return fsFileRead(&m_file, off, buf, size, option, bytes_read);
    }
    Result GetSize(s64* size) { return fsFileGetSize(&m_file, size); }

    FsFile m_file{};
    bool m_open = false;
};

struct Dir {
    Dir() = default;
    Dir(const Dir&) = delete;
    Dir& operator=(const Dir&) = delete;
    ~Dir() {
        if (m_open) fsDirClose(&m_dir);
    }
    Result Read(s64* total, size_t max, FsDirectoryEntry* buf) { return fsDirRead(&m_dir, total, max, buf); }

    FsDir m_dir{};
    bool m_open = false;
};

// A file system opened by the id of a title and the path of one of its NCAs.
struct FsNativeId {
    FsNativeId(u64 id, FsFileSystemType type, const char* path, FsContentAttributes attr) {
        FsPath p{path};
        if (hosversionAtLeast(16, 0, 0)) {
            m_rc = fsOpenFileSystemWithId(&m_fs, id, type, p.s, attr);
        } else {
            m_rc = fsOpenFileSystemWithId(&m_fs, id, type, p.s, FsContentAttributes_None);
        }
    }
    FsNativeId(const FsNativeId&) = delete;
    FsNativeId& operator=(const FsNativeId&) = delete;
    ~FsNativeId() {
        if (R_SUCCEEDED(m_rc)) fsFsClose(&m_fs);
    }
    Result GetFsOpenResult() const { return m_rc; }
    Result OpenFile(const char* path, u32 mode, File* f) {
        FsPath p{path};
        R_TRY(fsFsOpenFile(&m_fs, p.s, mode, &f->m_file));
        f->m_open = true;
        R_SUCCEED();
    }
    Result OpenDirectory(const char* path, u32 mode, Dir* d) {
        FsPath p{path};
        R_TRY(fsFsOpenDirectory(&m_fs, p.s, mode, &d->m_dir));
        d->m_open = true;
        R_SUCCEED();
    }

    FsFileSystem m_fs{};
    Result m_rc{};
};

// Only declared so the signatures of the core stay as they are; NXDock never reads tickets
// from a file system (it passes nullptr).
struct Fs {
    Result read_entire_file(const char*, std::vector<u8>&) { return MAKERESULT(Module_Libnx, LibnxError_NotFound); }
};

} // namespace sphaira::fs
