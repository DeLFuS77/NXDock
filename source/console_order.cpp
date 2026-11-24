// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "console_order.hpp"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace nxdock {
namespace {

constexpr unsigned char kTypeGame = 0x80; // NcmContentMetaType_Application

// The SD card first, then both, then the internal memory; archived games last.
int WhereRank(const InstalledApp& a) {
    if (a.archived || a.Bytes() == 0) return 3;
    if (a.sd_bytes && a.nand_bytes) return 1;
    return a.nand_bytes ? 2 : 0;
}

} // namespace

void SortConsole(std::vector<InstalledApp>& apps, int sort) {
    if (sort <= kSortName || sort >= kSortCount) return;
    std::stable_sort(apps.begin(), apps.end(), [sort](const InstalledApp& a, const InstalledApp& b) {
        switch (sort) {
            case kSortSize: return a.Bytes() > b.Bytes();
            case kSortLast: {
                // never played (or not known) after all the others
                const long long x = a.idle_minutes < 0 ? (1ll << 62) : a.idle_minutes;
                const long long y = b.idle_minutes < 0 ? (1ll << 62) : b.idle_minutes;
                return x < y;
            }
            case kSortTime: return std::max(0ll, a.play_minutes) > std::max(0ll, b.play_minutes);
            case kSortWhere: return WhereRank(a) < WhereRank(b);
            default: return false;
        }
    });
}

bool WithoutGame(const InstalledApp& a) {
    if (a.archived || a.contents.empty()) return false;
    return std::none_of(a.contents.begin(), a.contents.end(), [](const InstalledApp::Content& c) { return c.type == kTypeGame; });
}

bool NotOnHome(const InstalledApp& a) {
    return a.hidden && !a.archived && !WithoutGame(a);
}

std::vector<InstalledApp::Content> HomeRecordContents(const std::vector<InstalledApp::Content>& contents) {
    constexpr unsigned char kTypeUpdate = 0x81, kTypeDlc = 0x82; // NcmContentMetaType_Patch, _AddOnContent
    std::vector<InstalledApp::Content> out;
    for (const auto& c : contents) {
        if (c.type == kTypeGame) out.push_back(c);
    }
    if (out.empty()) return {};
    const InstalledApp::Content* newest = nullptr;
    for (const auto& c : contents) {
        if (c.type == kTypeUpdate && (!newest || c.version > newest->version)) newest = &c;
    }
    if (newest) out.push_back(*newest);
    for (const auto& c : contents) {
        if (c.type == kTypeDlc) out.push_back(c);
    }
    return out;
}

std::string PlayTimeText(long long minutes, const char* h_min_fmt, const char* h_fmt, const char* min_fmt) {
    if (minutes < 0) minutes = 0;
    char text[64];
    const long long h = minutes / 60, m = minutes % 60;
    if (h && m) std::snprintf(text, sizeof(text), h_min_fmt, int(h), int(m));
    else if (h) std::snprintf(text, sizeof(text), h_fmt, int(h));
    else std::snprintf(text, sizeof(text), min_fmt, int(m));
    return text;
}

std::string AgoText(long long minutes, const char* today, const char* yesterday, const char* days_fmt, const char* weeks_fmt,
                    const char* months_fmt) {
    const long long days = std::max(0ll, minutes) / (60 * 24);
    char text[64];
    if (days == 0) return today;
    if (days == 1) return yesterday;
    if (days < 14) std::snprintf(text, sizeof(text), days_fmt, int(days));
    else if (days < 61) std::snprintf(text, sizeof(text), weeks_fmt, int(days / 7));
    else std::snprintf(text, sizeof(text), months_fmt, int(days / 30));
    return text;
}

int ModFolders(const std::string& contents_dir) {
    DIR* d = opendir(contents_dir.c_str());
    if (!d) return -1;
    int n = 0;
    while (const dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        struct stat st{};
        if (stat((contents_dir + "/" + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) n++;
    }
    closedir(d);
    return n;
}

unsigned long long AppOfTitle(unsigned long long tid) {
    if ((tid & 0xFFF) == 0x800) return tid ^ 0x800;
    if ((tid & 0xFFF) != 0) return (tid - 0x1000) & ~0xFFFull;
    return tid;
}

unsigned long long TitleOfRights(const unsigned char rights[16]) {
    unsigned long long tid = 0;
    for (int i = 0; i < 8; i++) tid = tid << 8 | rights[i];
    return tid;
}

std::string TicketFileName(const unsigned char rights[16], const char* ext) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 16; i++) {
        out += hex[rights[i] >> 4];
        out += hex[rights[i] & 15];
    }
    return out + ext;
}

} // namespace nxdock
