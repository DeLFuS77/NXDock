// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "history.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nxdock {
namespace {

// a field never holds a tab or a line break: they would cut the line
std::string Clean(const std::string& s) {
    std::string out = s;
    for (auto& c : out) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

const char* ResultWord(HistoryEntry::Result r) {
    switch (r) {
        case HistoryEntry::Result::Installed: return "ok";
        case HistoryEntry::Result::Failed: return "failed";
        case HistoryEntry::Result::Cancelled: return "cancelled";
        case HistoryEntry::Result::Removed: return "removed";
    }
    return "ok";
}

std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> lines;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) {
        return lines;
    }
    std::string line;
    char buf[1024];
    while (std::fgets(buf, sizeof(buf), f)) {
        line += buf;
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
            line.clear();
        }
    }
    if (!line.empty()) lines.push_back(line);
    std::fclose(f);
    return lines;
}

bool WriteLines(const std::string& path, const std::vector<std::string>& lines) {
    const std::string temp = path + ".tmp";
    FILE* f = std::fopen(temp.c_str(), "w");
    if (!f) {
        return false;
    }
    bool ok = true;
    for (const auto& l : lines) {
        ok = ok && std::fputs(l.c_str(), f) >= 0 && std::fputc('\n', f) != EOF;
    }
    ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        std::remove(temp.c_str());
        return false;
    }
    std::remove(path.c_str());
    return std::rename(temp.c_str(), path.c_str()) == 0;
}

} // namespace

const char* HistoryPath() {
    return "sdmc:/switch/NXDock/history.tsv";
}

std::string History::Format(const HistoryEntry& e) {
    char nums[160];
    std::snprintf(nums, sizeof(nums), "%lld\t%s\t%llu\t%llu\t%.1f\t%s", (long long)e.when, e.nand ? "nand" : "sd",
                  (unsigned long long)e.size, (unsigned long long)e.written, e.seconds, ResultWord(e.result));
    return "1\t" + std::string(nums) + "\t" + Clean(e.format) + "\t" + Clean(e.game) + "\t" + Clean(e.label) + "\t" + Clean(e.error);
}

bool History::Parse(const std::string& line, HistoryEntry& out) {
    std::vector<std::string> f;
    size_t start = 0;
    while (true) {
        const size_t tab = line.find('\t', start);
        f.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    if (f.size() < 11 || f[0] != "1") {
        return false;
    }
    HistoryEntry e;
    e.when = std::strtoll(f[1].c_str(), nullptr, 10);
    e.nand = f[2] == "nand";
    e.size = std::strtoull(f[3].c_str(), nullptr, 10);
    e.written = std::strtoull(f[4].c_str(), nullptr, 10);
    e.seconds = std::strtod(f[5].c_str(), nullptr);
    if (f[6] == "ok") e.result = HistoryEntry::Result::Installed;
    else if (f[6] == "failed") e.result = HistoryEntry::Result::Failed;
    else if (f[6] == "cancelled") e.result = HistoryEntry::Result::Cancelled;
    else if (f[6] == "removed") e.result = HistoryEntry::Result::Removed;
    else return false;
    e.format = f[7];
    e.game = f[8];
    e.label = f[9];
    e.error = f[10];
    out = std::move(e);
    return true;
}

std::vector<HistoryEntry> History::Load() const {
    std::vector<HistoryEntry> out;
    for (const auto& l : ReadLines(m_path)) {
        HistoryEntry e;
        if (Parse(l, e)) out.push_back(std::move(e));
    }
    std::reverse(out.begin(), out.end());
    return out;
}

bool History::Append(const HistoryEntry& entry) const {
    auto lines = ReadLines(m_path);
    lines.push_back(Format(entry));
    if (lines.size() > kKeep) {
        lines.erase(lines.begin(), lines.end() - kKeep);
    }
    return WriteLines(m_path, lines);
}

bool History::Clear() const {
    std::remove(m_path.c_str());
    return true;
}

} // namespace nxdock
