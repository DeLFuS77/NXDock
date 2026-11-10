// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "mods.hpp"

#include "zip.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace nxdock {
namespace {

std::string Hex(unsigned long long id) {
    char t[20];
    std::snprintf(t, sizeof(t), "%016llX", id);
    return t;
}

bool IsHex16(const std::string& s) {
    return s.size() == 16 && std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit((unsigned char)c); });
}

std::string Upper(std::string s) {
    for (auto& c : s) c = char(std::toupper((unsigned char)c));
    return s;
}

std::vector<std::string> Split(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find('/', start);
        parts.push_back(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return parts;
}

bool Known(const std::string& part) {
    const std::string l = Upper(part);
    return l == "ROMFS" || l == "EXEFS" || l == "CHEATS";
}

void MakeDirs(const std::string& file) {
    for (size_t i = file.find('/', file.find(":/") == std::string::npos ? 1 : file.find(":/") + 2); i != std::string::npos; i = file.find('/', i + 1)) {
        mkdir(file.substr(0, i).c_str(), 0777);
    }
}

std::string StateFile(const std::string& state_dir, unsigned long long title_id, const std::string& pkg) {
    return state_dir + "/" + Hex(title_id) + "/" + pkg + ".txt";
}

std::vector<std::string> ReadList(const std::string& path) {
    std::vector<std::string> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (!l.empty()) out.push_back(l);
    }
    std::fclose(f);
    return out;
}

bool Exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

// Every list of the game: the files other packages put.
std::vector<std::string> OwnedByOthers(const std::string& state_dir, unsigned long long title_id, const std::string& pkg) {
    std::vector<std::string> out;
    const std::string dir = state_dir + "/" + Hex(title_id);
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (const dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() < 5 || n.substr(n.size() - 4) != ".txt" || n == pkg + ".txt") continue;
        const auto list = ReadList(dir + "/" + n);
        out.insert(out.end(), list.begin(), list.end());
    }
    closedir(d);
    return out;
}

} // namespace

std::string ModsDir() {
    return "sdmc:/switch/NXDock/mods";
}

std::string ModsStateDir() {
    return "sdmc:/switch/NXDock/mods/.installed";
}

std::string ContentsDir(unsigned long long title_id) {
    return "sdmc:/atmosphere/contents/" + Hex(title_id);
}

std::string ModTarget(const std::string& entry, unsigned long long title_id) {
    if (!zip::SafeName(entry) || entry.back() == '/') return {};
    auto parts = Split(entry);
    if (!parts.empty() && Upper(parts[0]) == "ATMOSPHERE") parts.erase(parts.begin());
    if (!parts.empty() && Upper(parts[0]) == "CONTENTS") parts.erase(parts.begin());
    if (!parts.empty() && IsHex16(parts[0])) {
        if (Upper(parts[0]) != Hex(title_id)) return {}; // for another game
        parts.erase(parts.begin());
    } else if (parts.size() > 1 && !Known(parts[0]) && Known(parts[1])) {
        parts.erase(parts.begin()); // the folder of the mod's name
    }
    if (parts.size() < 2 || !Known(parts[0])) return {};
    std::string out;
    for (size_t i = 0; i < parts.size(); i++) out += (i ? "/" : "") + (i == 0 ? std::string(Upper(parts[0]) == "CHEATS" ? "cheats" : Upper(parts[0]) == "ROMFS" ? "romfs" : "exefs") : parts[i]);
    return out;
}

std::vector<ModPackage> ModPackages(const std::string& mods_dir, const std::string& state_dir, unsigned long long title_id) {
    std::vector<ModPackage> out;
    for (const std::string& dir : {mods_dir + "/" + Hex(title_id), mods_dir}) {
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        std::vector<std::string> names;
        while (const dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > 4 && Upper(n.substr(n.size() - 4)) == ".ZIP") names.push_back(n);
        }
        closedir(d);
        std::sort(names.begin(), names.end());
        for (const auto& n : names) {
            ModPackage p;
            p.name = n.substr(0, n.size() - 4);
            p.path = dir + "/" + n;
            zip::Reader r;
            if (!r.Open(p.path)) continue;
            for (const auto& e : r.Entries()) {
                if (!ModTarget(e.name, title_id).empty()) {
                    p.files++;
                    p.bytes += e.size;
                }
            }
            if (!p.files) continue;
            if (std::any_of(out.begin(), out.end(), [&](const ModPackage& o) { return o.name == p.name; })) continue;
            p.on = Exists(StateFile(state_dir, title_id, p.name));
            out.push_back(p);
        }
    }
    return out;
}

bool InstallMod(const ModPackage& pkg, unsigned long long title_id, const std::string& contents_dir, const std::string& state_dir,
                std::string& conflict) {
    zip::Reader r;
    if (!r.Open(pkg.path)) return false;
    const auto others = OwnedByOthers(state_dir, title_id, pkg.name);
    const auto mine = ReadList(StateFile(state_dir, title_id, pkg.name));
    std::vector<std::pair<const zip::Entry*, std::string>> todo;
    for (const auto& e : r.Entries()) {
        const std::string target = ModTarget(e.name, title_id);
        if (target.empty()) continue;
        // never over another mod's file, nor over one that was there before
        const bool ours = std::find(mine.begin(), mine.end(), target) != mine.end();
        if (std::find(others.begin(), others.end(), target) != others.end() || (!ours && Exists(contents_dir + "/" + target))) {
            conflict = target;
            return false;
        }
        todo.push_back({&e, target});
    }
    // the list first: what is written can always be taken away again
    const std::string state = StateFile(state_dir, title_id, pkg.name);
    MakeDirs(state);
    FILE* list = std::fopen(state.c_str(), "wb");
    if (!list) return false;
    for (const auto& t : todo) std::fprintf(list, "%s\n", t.second.c_str());
    std::fclose(list);
    for (const auto& [entry, target] : todo) {
        const std::string path = contents_dir + "/" + target;
        MakeDirs(path);
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        const bool ok = r.Extract(*entry, [f](const void* d, size_t n) { return std::fwrite(d, 1, n, f) == n; });
        if (std::fclose(f) != 0 || !ok) return false;
    }
    return true;
}

bool RemoveMod(const ModPackage& pkg, unsigned long long title_id, const std::string& contents_dir, const std::string& state_dir) {
    const std::string state = StateFile(state_dir, title_id, pkg.name);
    const auto list = ReadList(state);
    bool ok = true;
    std::vector<std::string> folders;
    for (const auto& target : list) {
        const std::string path = contents_dir + "/" + target;
        if (Exists(path) && std::remove(path.c_str()) != 0) ok = false;
        for (size_t i = target.find_last_of('/'); i != std::string::npos && i > 0; i = target.find_last_of('/', i - 1)) {
            folders.push_back(contents_dir + "/" + target.substr(0, i));
        }
    }
    // the folders left empty, the deepest first (rmdir leaves full ones)
    std::sort(folders.begin(), folders.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    folders.erase(std::unique(folders.begin(), folders.end()), folders.end());
    for (const auto& f : folders) rmdir(f.c_str());
    if (ok) std::remove(state.c_str());
    return ok;
}

} // namespace nxdock
