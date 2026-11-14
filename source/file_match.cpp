// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "file_match.hpp"

#include "console_order.hpp"

#include <algorithm>
#include <cstdlib>

namespace nxdock {

namespace {

int KindOf(unsigned long long id) {
    const unsigned long long low = id & 0xFFF;
    return low == 0 ? 0 : low == 0x800 ? 1 : 2;
}

} // namespace

bool ParseTitleName(const std::string& name, unsigned long long& id, unsigned& version) {
    id = 0;
    version = 0;
    for (size_t at = name.find('['); at != std::string::npos; at = name.find('[', at + 1)) {
        if (at + 18 > name.size() || name[at + 17] != ']' || name[at + 1] != '0') continue;
        unsigned long long v = 0;
        bool hex = true;
        for (size_t i = at + 1; i < at + 17 && hex; i++) {
            const char c = name[i];
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) hex = false;
            v = v << 4 | (unsigned long long)(d < 0 ? 0 : d);
        }
        if (!hex) continue;
        id = v;
        const size_t vat = name.find("[v", at + 18);
        if (vat != std::string::npos) version = unsigned(std::strtoul(name.c_str() + vat + 2, nullptr, 10));
        return true;
    }
    return false;
}

ConsoleHas HasOf(const std::vector<InstalledApp>& apps) {
    ConsoleHas has;
    for (const auto& a : apps) {
        if (a.archived) continue;
        for (const auto& c : a.contents) {
            auto& v = has.titles[c.id];
            v = std::max(v, c.version);
            if (KindOf(c.id) == 0) has.games.insert(c.id);
        }
    }
    return has;
}

FileMatch MatchFile(const ConsoleHas& has, unsigned long long id, unsigned version) {
    FileMatch m;
    if (!id) return m;
    const int kind = KindOf(id);
    const unsigned long long app = AppOfTitle(id);
    const bool game = has.games.count(app) > 0;
    const auto it = has.titles.find(id);
    if (kind == 0) {
        if (it == has.titles.end()) return m;
        m.installed = it->second;
        const auto upd = has.titles.find(app | 0x800);
        m.running = std::max(m.installed, upd == has.titles.end() ? 0u : upd->second);
        m.state = version < m.installed ? FileState::Older : FileState::Installed;
        return m;
    }
    if (it == has.titles.end()) {
        // not on the console: new for its game, when the console has the game
        if (game) m.state = FileState::ForGame;
        return m;
    }
    m.installed = it->second;
    if (version == m.installed) m.state = FileState::Installed;
    else if (version < m.installed) m.state = FileState::Older;
    else if (game) m.state = FileState::ForGame;
    return m;
}

UpdatePick PickUpdates(const ConsoleHas& has, const std::vector<FoundFile>& files) {
    UpdatePick pick;
    // the newest file of each title the console wants
    std::map<unsigned long long, size_t> best;
    for (size_t i = 0; i < files.size(); i++) {
        const auto& f = files[i];
        if (!f.id || KindOf(f.id) == 0 || !has.games.count(AppOfTitle(f.id))) continue;
        if (MatchFile(has, f.id, f.version).state != FileState::ForGame) {
            pick.left++;
            continue;
        }
        const auto it = best.find(f.id);
        if (it == best.end()) {
            best[f.id] = i;
        } else {
            pick.left++; // an older copy, or the same version twice
            if (files[it->second].version < f.version) it->second = i;
        }
    }
    for (const auto& [id, i] : best) pick.chosen.push_back(i);
    std::sort(pick.chosen.begin(), pick.chosen.end());
    return pick;
}

int InvertMarks(std::vector<char>& marks, const std::vector<char>& can) {
    int on = 0;
    for (size_t i = 0; i < marks.size(); i++) {
        if (can.empty() || (i < can.size() && can[i])) marks[i] = !marks[i];
        else marks[i] = 0;
        on += marks[i] != 0;
    }
    return on;
}

int InvertMarks(std::set<unsigned long long>& marked, const std::vector<unsigned long long>& can) {
    std::set<unsigned long long> out;
    for (const auto id : can) {
        if (!marked.count(id)) out.insert(id);
    }
    marked = std::move(out);
    return int(marked.size());
}

} // namespace nxdock
