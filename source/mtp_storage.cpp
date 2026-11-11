// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "mtp_storage.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cstdio>
#include <vector>

namespace nxdock::mtp {
namespace {

class FileSource final : public Source {
public:
    explicit FileSource(FILE* f) : m_f(f) {}
    ~FileSource() override { std::fclose(m_f); }
    bool Read(uint64_t off, void* buf, size_t size) override {
        return fseeko(m_f, off_t(off), SEEK_SET) == 0 && std::fread(buf, 1, size, m_f) == size;
    }

private:
    FILE* m_f;
};

class FileSink final : public Sink {
public:
    // writes to path + ".part"; the real name only when the whole file arrived
    FileSink(FILE* f, std::string path) : m_f(f), m_path(std::move(path)), m_part(m_path + ".part") {
        m_buf.resize(1024 * 1024);
        std::setvbuf(m_f, m_buf.data(), _IOFBF, m_buf.size());
    }
    ~FileSink() override {
        if (m_f) Close(false);
    }
    bool Write(const void* data, size_t size) override { return std::fwrite(data, 1, size, m_f) == size; }
    bool Close(bool ok) override {
        const bool closed = std::fclose(m_f) == 0;
        m_f = nullptr;
        if (!ok || !closed) {
            std::remove(m_part.c_str()); // never half a file, and the old one stays
            return false;
        }
        if (std::rename(m_part.c_str(), m_path.c_str()) == 0) return true;
        // the SD card does not rename over a file: take the old one away first
        std::remove(m_path.c_str());
        return std::rename(m_part.c_str(), m_path.c_str()) == 0;
    }

private:
    FILE* m_f;
    std::string m_path, m_part;
    std::vector<char> m_buf;
};

bool RemoveTree(const std::string& path) {
    DIR* d = opendir(path.c_str());
    if (!d) return std::remove(path.c_str()) == 0;
    std::vector<std::string> names;
    while (const dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
    }
    closedir(d);
    bool ok = true;
    for (const auto& n : names) {
        const std::string p = path + "/" + n;
        struct stat st{};
        if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ok = RemoveTree(p) && ok;
        else ok = std::remove(p.c_str()) == 0 && ok;
    }
    return rmdir(path.c_str()) == 0 && ok;
}

} // namespace

std::string FolderStorage::Full(const std::string& path) const {
    if (path.empty()) return m_root;
    return !m_root.empty() && m_root.back() == '/' ? m_root + path : m_root + "/" + path;
}

void FolderStorage::Space(uint64_t& total, uint64_t& free) {
    struct statvfs st{};
    if (statvfs(m_root.c_str(), &st) == 0) {
        total = uint64_t(st.f_blocks) * st.f_frsize;
        free = uint64_t(st.f_bavail) * st.f_frsize;
    } else {
        total = free = 0;
    }
}

bool FolderStorage::List(const std::string& dir, std::vector<Entry>& out) {
    DIR* d = opendir(Full(dir).c_str());
    if (!d) return false;
    while (const dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        Entry entry;
        if (Stat(dir.empty() ? n : dir + "/" + n, entry)) out.push_back(entry);
    }
    closedir(d);
    return true;
}

bool FolderStorage::Stat(const std::string& path, Entry& out) {
    struct stat st{};
    if (stat(Full(path).c_str(), &st) != 0) return false;
    const auto slash = path.find_last_of('/');
    out.name = slash == std::string::npos ? path : path.substr(slash + 1);
    out.dir = S_ISDIR(st.st_mode);
    out.size = out.dir ? 0 : uint64_t(st.st_size);
    out.mtime = int64_t(st.st_mtime);
    return true;
}

std::unique_ptr<Source> FolderStorage::Open(const std::string& path) {
    FILE* f = std::fopen(Full(path).c_str(), "rb");
    return f ? std::make_unique<FileSource>(f) : nullptr;
}

std::unique_ptr<Sink> FolderStorage::Create(const std::string& path, uint64_t size, bool& busy) {
    busy = false;
    uint64_t total, free;
    Space(total, free);
    if (total && size > free) return nullptr;
    const std::string full = Full(path);
    FILE* f = std::fopen((full + ".part").c_str(), "wb");
    return f ? std::make_unique<FileSink>(f, full) : nullptr;
}

bool FolderStorage::MakeDir(const std::string& path) {
    return mkdir(Full(path).c_str(), 0777) == 0;
}

bool FolderStorage::Remove(const std::string& path, bool dir) {
    return dir ? RemoveTree(Full(path)) : std::remove(Full(path).c_str()) == 0;
}

bool FolderStorage::Rename(const std::string& from, const std::string& to) {
    return std::rename(Full(from).c_str(), Full(to).c_str()) == 0;
}

} // namespace nxdock::mtp
