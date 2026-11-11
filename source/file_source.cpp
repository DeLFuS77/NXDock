// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "file_source.hpp"

#include "defines.hpp"
#include "fileops.hpp"

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>

namespace nxdock {
namespace {

std::string LowerCase(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

bool IsDir(const std::string& path, unsigned long long* size = nullptr) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) return false;
    if (size) *size = (unsigned long long)st.st_size;
    return S_ISDIR(st.st_mode);
}

std::string Join(const std::string& folder, const std::string& name) {
    return !folder.empty() && folder.back() == '/' ? folder + name : folder + "/" + name;
}

// A part number at the end of a name: "00".."99" after a dot, or the whole name of two digits.
int PartNumber(const std::string& s) {
    if (s.size() != 2 || !std::isdigit((unsigned char)s[0]) || !std::isdigit((unsigned char)s[1])) return -1;
    return (s[0] - '0') * 10 + (s[1] - '0');
}

// The parts 00, 01… of a split folder, in order; empty when it is not one.
std::vector<std::string> FolderParts(const std::string& path) {
    std::vector<std::pair<int, std::string>> found;
    DIR* d = opendir(path.c_str());
    if (!d) return {};
    while (const dirent* e = readdir(d)) {
        const int n = PartNumber(e->d_name);
        if (n >= 0) found.push_back({n, Join(path, e->d_name)});
    }
    closedir(d);
    std::sort(found.begin(), found.end());
    std::vector<std::string> parts;
    for (size_t i = 0; i < found.size(); i++) {
        if (found[i].first != int(i)) return {}; // a missing part: not offered
        parts.push_back(found[i].second);
    }
    return parts;
}

} // namespace

FileSource::FileSource(std::vector<std::string> parts) {
    s64 start = 0;
    for (auto& p : parts) {
        Part part;
        part.path = std::move(p);
        part.file = std::fopen(part.path.c_str(), "rb");
        if (!part.file) {
            m_open_result = Result_UnknownContainer;
            break;
        }
        // a large buffer: the SD card reads much faster in big pieces
        m_buffers.emplace_back(1024 * 1024);
        std::setvbuf(part.file, m_buffers.back().data(), _IOFBF, m_buffers.back().size());
        std::fseek(part.file, 0, SEEK_END);
        part.size = s64(ftello(part.file));
        part.start = start;
        start += part.size;
        m_parts.push_back(part);
    }
    m_size = start;
    if (m_parts.empty() && R_SUCCEEDED(m_open_result)) {
        m_open_result = Result_UnknownContainer;
    }
}

FileSource::~FileSource() {
    for (auto& p : m_parts) {
        if (p.file) std::fclose(p.file);
    }
}

Result FileSource::Read(void* buf, s64 off, s64 size, u64* bytes_read) {
    std::lock_guard lock(m_mutex);
    auto out = static_cast<unsigned char*>(buf);
    *bytes_read = 0;
    R_UNLESS(off >= 0 && off + size <= m_size, Result_YatiInvalidNcaReadSize);
    while (size > 0) {
        const auto it = std::find_if(m_parts.begin(), m_parts.end(), [off](const Part& p) { return off >= p.start && off < p.start + p.size; });
        R_UNLESS(it != m_parts.end(), Result_YatiInvalidNcaReadSize);
        const s64 in_part = off - it->start;
        const s64 n = std::min(size, it->size - in_part);
        R_UNLESS(fseeko(it->file, in_part, SEEK_SET) == 0, Result_YatiInvalidNcaReadSize);
        const size_t got = std::fread(out, 1, size_t(n), it->file);
        R_UNLESS(got == size_t(n), Result_YatiInvalidNcaReadSize);
        out += n;
        off += n;
        size -= n;
        *bytes_read += u64(n);
    }
    R_SUCCEED();
}

std::string GameFormat(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return {};
    const std::string ext = LowerCase(name.substr(dot + 1));
    return ext == "nsp" || ext == "nsz" || ext == "xci" || ext == "xcz" ? ext : std::string{};
}

std::vector<LocalEntry> ListFolder(const std::string& folder, bool root, bool all) {
    std::vector<LocalEntry> dirs, files;
    DIR* d = opendir(folder.c_str());
    if (!d) return {};
    std::vector<std::string> names;
    while (const dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == ".." || name.empty() || name[0] == '.') continue;
        if (all && fileops::Junk(name)) continue;
        names.push_back(name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());

    // name.nsp.00, .01…: one entry for all its parts
    std::vector<std::string> split_seen;
    for (const auto& name : names) {
        const std::string path = Join(folder, name);
        unsigned long long size = 0;
        if (IsDir(path, &size)) {
            const std::string fmt = GameFormat(name);
            if (!fmt.empty()) {
                // a split folder (FAT32): the parts 00, 01… inside
                auto parts = FolderParts(path);
                if (!parts.empty()) {
                    LocalEntry f{name, path, parts, false, 0, fmt};
                    for (const auto& p : parts) {
                        unsigned long long ps = 0;
                        IsDir(p, &ps);
                        f.size += ps;
                    }
                    files.push_back(std::move(f));
                    continue;
                }
            }
            dirs.push_back({name, path, {}, true, 0, {}});
            continue;
        }
        const auto dot = name.find_last_of('.');
        if (dot != std::string::npos && PartNumber(name.substr(dot + 1)) >= 0) {
            const std::string whole = name.substr(0, dot);
            if (GameFormat(whole).empty()) {
                if (all) files.push_back({name, path, {}, false, size, {}}); // name.zip.01: a file of its own
                continue;
            }
            if (std::find(split_seen.begin(), split_seen.end(), whole) != split_seen.end()) continue;
            split_seen.push_back(whole);
            LocalEntry f{whole, path, {}, false, 0, GameFormat(whole)};
            for (int n = 0;; n++) {
                char suffix[16];
                std::snprintf(suffix, sizeof(suffix), ".%02d", n);
                const std::string p = Join(folder, whole + suffix);
                unsigned long long ps = 0;
                struct stat st{};
                if (stat(p.c_str(), &st) != 0) break;
                ps = (unsigned long long)st.st_size;
                f.parts.push_back(p);
                f.size += ps;
            }
            if (!f.parts.empty() && f.parts.front() == Join(folder, whole + ".00")) files.push_back(std::move(f));
            continue;
        }
        const std::string fmt = GameFormat(name);
        if (!fmt.empty()) {
            files.push_back({name, path, {path}, false, size, fmt});
        } else if (all) {
            files.push_back({name, path, {}, false, size, {}});
        }
    }
    std::vector<LocalEntry> out;
    if (!root) {
        const auto slash = folder.find_last_of('/', folder.size() > 1 && folder.back() == '/' ? folder.size() - 2 : std::string::npos);
        std::string up = slash == std::string::npos ? folder : folder.substr(0, slash + 1);
        out.push_back({"..", up, {}, true, 0, {}});
    }
    out.insert(out.end(), dirs.begin(), dirs.end());
    out.insert(out.end(), files.begin(), files.end());
    return out;
}

} // namespace nxdock
