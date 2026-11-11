// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "fileops.hpp"

#include "file_source.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace nxdock::fileops {
namespace {

std::string Lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

// The path without its last slash, as stat and rename want it ("sdmc:/a/" -> "sdmc:/a"); a root
// keeps it ("sdmc:/").
std::string Bare(std::string path) {
    while (path.size() > 1 && path.back() == '/' && path[path.size() - 2] != ':') path.pop_back();
    return path;
}

bool StatOf(const std::string& path, struct stat& st) { return stat(Bare(path).c_str(), &st) == 0; }

bool IsDirectory(const std::string& path) {
    struct stat st{};
    return StatOf(path, st) && S_ISDIR(st.st_mode);
}

bool Exists(const std::string& path) {
    struct stat st{};
    return StatOf(path, st);
}

std::vector<std::string> Children(const std::string& folder) {
    std::vector<std::string> out;
    DIR* d = opendir(Bare(folder).c_str());
    if (!d) return out;
    while (const dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        out.push_back(name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

// The path after "sdmc:/", lower case, without slashes at its ends: "atmosphere/contents".
bool SdRelative(const std::string& path, std::string& rel) {
    const std::string p = Lower(path);
    if (p.rfind("sdmc:/", 0) != 0) return false;
    rel = p.substr(6);
    while (!rel.empty() && rel.back() == '/') rel.pop_back();
    while (!rel.empty() && rel.front() == '/') rel.erase(0, 1);
    return true;
}

bool Under(const std::string& rel, const char* top) {
    const std::string t = top;
    return rel == t || rel.rfind(t + "/", 0) == 0;
}

void CountInto(const std::string& path, Count& c, int depth) {
    struct stat st{};
    if (!StatOf(path, st)) return;
    if (!S_ISDIR(st.st_mode)) {
        c.files++;
        c.bytes += (unsigned long long)st.st_size;
        return;
    }
    c.folders++;
    if (depth > 64) return;
    for (const auto& n : Children(path)) CountInto(Join(path, n), c, depth + 1);
}

bool DeleteTree(const std::string& path, Progress& p, int depth) {
    if (p.cancel) return false;
    struct stat st{};
    if (!StatOf(path, st)) return true; // gone already
    if (S_ISDIR(st.st_mode)) {
        if (depth > 64) return false;
        for (const auto& n : Children(path)) {
            if (!DeleteTree(Join(path, n), p, depth + 1)) return false;
        }
        if (rmdir(Bare(path).c_str()) != 0) {
            std::lock_guard lock(p.mutex);
            p.error = path;
            return false;
        }
        return true;
    }
    p.SetCurrent(NameOf(path));
    if (unlink(Bare(path).c_str()) != 0) {
        std::lock_guard lock(p.mutex);
        p.error = path;
        return false;
    }
    p.files_done++;
    p.bytes_done += (unsigned long long)st.st_size;
    return true;
}

// A file opened to be written with `size` bytes. On the SD card a file of 4 GB or more is made
// first as a big file, which the console keeps as a folder of parts when the card is FAT32 and
// reads as one file.
FILE* OpenOut(const std::string& path, unsigned long long size) {
#ifdef __SWITCH__
    if (size > kFat32Max && Lower(path).rfind("sdmc:/", 0) == 0) {
        if (FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc")) {
            char inner[FS_MAX_PATH] = {};
            std::snprintf(inner, sizeof(inner), "%s", path.c_str() + 5); // after "sdmc:"
            if (R_SUCCEEDED(fsFsCreateFile(fs, inner, s64(size), FsCreateOption_BigFile))) {
                return std::fopen(path.c_str(), "r+b");
            }
        }
    }
#else
    (void)size;
#endif
    return std::fopen(path.c_str(), "wb");
}

constexpr size_t kBuffer = 4u << 20;

// Copies one file to `to`, or to a folder of parts at `to` when `split` (see CopyAll). Gives false
// on a failure or a stop, with nothing left at `to`.
struct Limits {
    unsigned long long max_file, part;
};

bool CopyFile(const std::string& from, const std::string& to, bool split, const Limits& lim, Progress& p, std::vector<char>& buf) {
    struct stat st{};
    if (!StatOf(from, st)) return false;
    const unsigned long long size = (unsigned long long)st.st_size;
    p.SetCurrent(NameOf(from));
    FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) return false;
    const bool parts = split && size > lim.max_file;
    if (parts && mkdir(to.c_str(), 0777) != 0) {
        std::fclose(in);
        return false;
    }
    bool ok = true;
    unsigned long long left = size;
    for (int part = 0; ok && (left > 0 || part == 0); part++) {
        char name[16];
        std::snprintf(name, sizeof(name), "%02d", part);
        const std::string out_path = parts ? Join(to, name) : to;
        const unsigned long long want = parts ? std::min(left, lim.part) : left;
        FILE* out = OpenOut(out_path, want);
        if (!out) {
            ok = false;
            break;
        }
        unsigned long long n = want;
        while (n > 0) {
            if (p.cancel) {
                ok = false;
                break;
            }
            const size_t step = size_t(std::min<unsigned long long>(n, buf.size()));
            if (std::fread(buf.data(), 1, step, in) != step || std::fwrite(buf.data(), 1, step, out) != step) {
                ok = false;
                break;
            }
            n -= step;
            p.bytes_done += step;
        }
        if (std::fclose(out) != 0) ok = false;
        left -= want - n;
        if (!parts) break;
    }
    std::fclose(in);
    if (ok) {
        // read back by its size
        unsigned long long got = 0;
        if (parts) {
            for (const auto& n : Children(to)) {
                struct stat ps{};
                if (StatOf(Join(to, n), ps)) got += (unsigned long long)ps.st_size;
            }
        } else {
            struct stat ds{};
            if (StatOf(to, ds)) got = (unsigned long long)ds.st_size;
        }
        ok = got == size;
    }
    if (!ok) {
        // nothing half written is left
        Progress scratch;
        DeleteTree(to, scratch, 0);
        if (!p.cancel) {
            std::lock_guard lock(p.mutex);
            p.error = from;
        }
        return false;
    }
    p.files_done++;
    return true;
}

bool CopyTree(const std::string& from, const std::string& to, bool move, bool split, const Limits& lim, Progress& p, std::vector<char>& buf, int depth) {
    if (p.cancel) return false;
    if (IsDirectory(from)) {
        if (depth > 64) return false;
        if (!Exists(to) && mkdir(Bare(to).c_str(), 0777) != 0) {
            std::lock_guard lock(p.mutex);
            p.error = to;
            return false;
        }
        for (const auto& n : Children(from)) {
            if (!CopyTree(Join(from, n), Join(to, n), move, split, lim, p, buf, depth + 1)) return false;
        }
        if (move && rmdir(Bare(from).c_str()) != 0) {
            std::lock_guard lock(p.mutex);
            p.error = from;
            return false;
        }
        return true;
    }
    if (!CopyFile(from, to, split, lim, p, buf)) return false;
    if (move && unlink(Bare(from).c_str()) != 0) {
        std::lock_guard lock(p.mutex);
        p.error = from;
        return false;
    }
    return true;
}

} // namespace

std::string Join(const std::string& folder, const std::string& name) {
    return !folder.empty() && folder.back() == '/' ? folder + name : folder + "/" + name;
}

std::string Parent(const std::string& path) {
    const std::string b = Bare(path);
    const auto slash = b.find_last_of('/');
    if (slash == std::string::npos) return b;
    return b.substr(0, slash + 1);
}

std::string NameOf(const std::string& path) {
    const std::string b = Bare(path);
    const auto slash = b.find_last_of('/');
    return slash == std::string::npos ? b : b.substr(slash + 1);
}

std::string DeviceOf(const std::string& path) {
    const auto colon = path.find(':');
    return colon == std::string::npos ? std::string{} : path.substr(0, colon + 1);
}

Guard GuardOf(const std::string& path) {
    std::string rel;
    if (!SdRelative(path, rel)) return Guard::Free; // a USB disk: the user's own
    if (rel.empty()) return Guard::System;
    if (rel == "switch/nxdock" || rel.rfind("switch/nxdock/nxdock.nro", 0) == 0 || Under(rel, "config/nxdock")) return Guard::Self;
    if (Under(rel, "nintendo") || Under(rel, "emummc") || Under(rel, "bootloader") || Under(rel, "emutendo")) return Guard::System;
    if (rel == "atmosphere" || rel == "switch" || rel == "config" || rel == "hbmenu.nro") return Guard::System;
    if (Under(rel, "atmosphere")) return Guard::Careful;
    return Guard::Free;
}

Guard GuardOfAll(const std::vector<std::string>& paths) {
    Guard worst = Guard::Free;
    for (const auto& p : paths) worst = std::max(worst, GuardOf(p));
    return worst;
}

bool Junk(const std::string& name) {
    if (name.empty() || name[0] == '.') return true; // hidden, ._* of macOS, .Trashes, .Spotlight…
    const std::string l = Lower(name);
    return l == "$recycle.bin" || l == "system volume information" || l == "thumbs.db" || l == "desktop.ini";
}

bool ValidName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 255) return false;
    if (name.back() == ' ' || name.back() == '.') return false;
    for (const unsigned char c : name) {
        if (c < 0x20 || std::string("/\\:*?\"<>|").find(char(c)) != std::string::npos) return false;
    }
    return true;
}

Count CountTree(const std::string& path) {
    Count c;
    CountInto(path, c, 0);
    return c;
}

Count CountAll(const std::vector<std::string>& paths) {
    Count c;
    for (const auto& p : paths) CountInto(p, c, 0);
    return c;
}

bool DeleteAll(const std::vector<std::string>& paths, Progress& p) {
    const Count c = CountAll(paths);
    p.files_total = c.files;
    p.bytes_total = c.bytes;
    bool ok = true;
    for (const auto& path : paths) {
        if (!DeleteTree(path, p, 0)) {
            ok = false;
            break;
        }
    }
    if (!ok && !p.cancel) p.failed = true;
    p.done = true;
    return ok;
}

bool Within(const std::string& inside, const std::string& folder) {
    const std::string a = Lower(Bare(inside)) + "/", b = Lower(Bare(folder)) + "/";
    return a.rfind(b, 0) == 0;
}

std::string FreeName(const std::string& folder, const std::string& name) {
    if (!Exists(Join(folder, name))) return name;
    // "game.nsp" -> "game (2).nsp"; a folder or a name without a dot: "folder (2)"
    const auto dot = name.find_last_of('.');
    const bool ext = dot != std::string::npos && dot > 0 && !IsDirectory(Join(folder, name));
    const std::string stem = ext ? name.substr(0, dot) : name, tail = ext ? name.substr(dot) : std::string{};
    for (int n = 2; n < 1000; n++) {
        const std::string candidate = stem + " (" + std::to_string(n) + ")" + tail;
        if (!Exists(Join(folder, candidate))) return candidate;
    }
    return name;
}

bool CopyAll(const std::vector<std::string>& paths, const std::string& folder, bool move, bool split_big, Progress& p,
             unsigned long long max_file, unsigned long long part) {
    const Limits lim{max_file, part};
    const Count c = CountAll(paths);
    p.files_total = c.files;
    p.bytes_total = c.bytes;
    std::vector<char> buf(kBuffer);
    bool ok = true;
    for (const auto& from : paths) {
        if (p.cancel) {
            ok = false;
            break;
        }
        if (Within(folder, from)) {
            // a folder into itself
            std::lock_guard lock(p.mutex);
            p.error = from;
            ok = false;
            break;
        }
        const std::string name = FreeName(folder, NameOf(from));
        const std::string to = Join(folder, name);
        if (move && DeviceOf(from) == DeviceOf(folder)) {
            // the same device: a rename, at once
            const Count one = CountTree(from);
            p.SetCurrent(NameOf(from));
            if (std::rename(Bare(from).c_str(), Bare(to).c_str()) != 0) {
                std::lock_guard lock(p.mutex);
                p.error = from;
                ok = false;
                break;
            }
            p.files_done += one.files;
            p.bytes_done += one.bytes;
            std::lock_guard lock(p.mutex);
            p.made.push_back(name);
            continue;
        }
        if (!CopyTree(from, to, move, split_big, lim, p, buf, 0)) {
            ok = false;
            // a folder copied in part stays as it is (its files are whole); a file half written is gone
            if (IsDirectory(to)) {
                std::lock_guard lock(p.mutex);
                p.made.push_back(name);
            }
            break;
        }
        std::lock_guard lock(p.mutex);
        p.made.push_back(name);
    }
    if (!ok && !p.cancel) p.failed = true;
    p.done = true;
    return ok;
}

bool MakeFolder(const std::string& folder, const std::string& name) {
    if (!ValidName(name)) return false;
    const std::string path = Join(folder, name);
    if (Exists(path)) return false;
    return mkdir(path.c_str(), 0777) == 0;
}

bool Rename(const std::string& path, const std::string& name) {
    if (!ValidName(name)) return false;
    const std::string to = Join(Parent(path), name);
    if (Lower(Bare(to)) != Lower(Bare(path)) && Exists(to)) return false;
    return std::rename(Bare(path).c_str(), to.c_str()) == 0;
}

std::vector<GameFile> GamesUnder(const std::vector<std::string>& paths, int max_depth) {
    std::vector<GameFile> out;
    struct Walk {
        static void Go(const std::string& path, int depth, int max, std::vector<GameFile>& out) {
            if (depth > max) return;
            for (const auto& e : ListFolder(path, true)) {
                if (e.dir) {
                    Go(e.path, depth + 1, max, out);
                } else if (!e.format.empty()) {
                    out.push_back({e.name, e.parts, e.size, e.format});
                }
            }
        }
    };
    for (const auto& p : paths) {
        if (IsDirectory(p)) {
            // a split folder (game.nsp/00, 01…) is a game, not a folder to go through
            const auto parent = ListFolder(Parent(p), true);
            const auto it = std::find_if(parent.begin(), parent.end(), [&](const LocalEntry& e) { return Bare(e.path) == Bare(p); });
            if (it != parent.end() && !it->dir) {
                out.push_back({it->name, it->parts, it->size, it->format});
                continue;
            }
            Walk::Go(p, 0, max_depth, out);
        }
    }
    return out;
}

} // namespace nxdock::fileops
