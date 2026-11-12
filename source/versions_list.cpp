// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "versions_list.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>

#ifdef __SWITCH__
#include <curl/curl.h>
#endif

namespace nxdock {
namespace {

constexpr const char* kPath = "sdmc:/switch/NXDock/versions.txt";
constexpr const char* kTemp = "sdmc:/switch/NXDock/versions.txt.new";
constexpr const char* kUrl = "https://raw.githubusercontent.com/blawar/titledb/master/versions.txt";

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

#ifdef __SWITCH__
size_t OnBody(char* data, size_t size, size_t count, void* user) {
    auto* out = static_cast<std::string*>(user);
    const size_t n = size * count;
    if (out->size() + n > (24u << 20)) return 0;
    out->append(data, n);
    return n;
}

int OnProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<const std::atomic<bool>*>(user)->load() ? 1 : 0;
}

bool Download(std::string& body, const std::atomic<bool>* stop) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    curl_easy_setopt(curl, CURLOPT_URL, kUrl);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // what it says is only shown, never installed
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXDock/" NXDOCK_VERSION);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 8000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*>(stop));
    long status = 0;
    const bool ok = curl_easy_perform(curl) == CURLE_OK && curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK && status == 200;
    curl_easy_cleanup(curl);
    return ok;
}
#endif

bool ReadFile(const char* path, std::string& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    out.clear();
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

} // namespace

bool VersionsList::Parse(const std::string& text, std::vector<std::pair<uint64_t, uint32_t>>& out) {
    out.clear();
    size_t at = text.find('\n');
    if (text.rfind("id|", 0) != 0 || at == std::string::npos) return false;
    at++;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        // 16 hex digits, "|", the rights ID, "|", the version (empty for none)
        if (end - at > 18 && text[at + 16] == '|') {
            uint64_t id = 0;
            bool hex = true;
            for (size_t i = 0; i < 16 && hex; i++) {
                const int d = HexDigit(text[at + i]);
                hex = d >= 0;
                id = (id << 4) | uint64_t(d < 0 ? 0 : d);
            }
            const size_t bar = text.find('|', at + 17);
            if (hex && bar != std::string::npos && bar < end) {
                uint32_t version = 0;
                for (size_t i = bar + 1; i < end && text[i] >= '0' && text[i] <= '9'; i++) version = version * 10 + uint32_t(text[i] - '0');
                out.push_back({id, version});
            }
        }
        at = end + 1;
    }
    std::sort(out.begin(), out.end());
    // one entry per title, its newest version
    std::vector<std::pair<uint64_t, uint32_t>> unique;
    for (const auto& e : out) {
        if (!unique.empty() && unique.back().first == e.first) unique.back().second = std::max(unique.back().second, e.second);
        else unique.push_back(e);
    }
    out = std::move(unique);
    return !out.empty();
}

void VersionsList::Set(std::vector<std::pair<uint64_t, uint32_t>> entries, long long made) {
    std::lock_guard lock(m_mutex);
    m_entries = std::move(entries);
    m_made = made;
}

bool VersionsList::Ready() const {
    std::lock_guard lock(m_mutex);
    return !m_entries.empty();
}

long long VersionsList::Made() const {
    std::lock_guard lock(m_mutex);
    return m_made;
}

uint32_t VersionsList::LatestUpdate(uint64_t app) const {
    std::lock_guard lock(m_mutex);
    const uint64_t patch = (app & ~uint64_t(0xFFF)) | 0x800;
    const auto it = std::lower_bound(m_entries.begin(), m_entries.end(), std::pair<uint64_t, uint32_t>{patch, 0});
    return it != m_entries.end() && it->first == patch ? it->second : 0;
}

std::vector<uint64_t> VersionsList::Dlcs(uint64_t app) const {
    std::lock_guard lock(m_mutex);
    std::vector<uint64_t> out;
    const uint64_t first = (app & ~uint64_t(0xFFF)) + 0x1001, last = (app & ~uint64_t(0xFFF)) + 0x1FFF;
    for (auto it = std::lower_bound(m_entries.begin(), m_entries.end(), std::pair<uint64_t, uint32_t>{first, 0});
         it != m_entries.end() && it->first <= last; ++it) {
        out.push_back(it->first);
    }
    return out;
}

void VersionsList::Start(bool refresh) {
    if (m_started.exchange(true)) return;
    m_thread = std::thread([this, refresh] {
        std::string text;
        struct stat st{};
        long long made = stat(kPath, &st) == 0 ? (long long)st.st_mtime : 0;
        std::vector<std::pair<uint64_t, uint32_t>> entries;
        if (made && ReadFile(kPath, text) && Parse(text, entries)) Set(entries, made);
#ifdef __SWITCH__
        const long long now = std::time(nullptr);
        if (refresh && (made == 0 || now - made > 24 * 3600)) {
            std::string fresh;
            std::vector<std::pair<uint64_t, uint32_t>> parsed;
            if (Download(fresh, &m_stop) && Parse(fresh, parsed)) {
                FILE* f = std::fopen(kTemp, "wb");
                if (f) {
                    const bool ok = std::fwrite(fresh.data(), 1, fresh.size(), f) == fresh.size();
                    if (std::fclose(f) == 0 && ok) {
                        std::remove(kPath);
                        std::rename(kTemp, kPath);
                    } else {
                        std::remove(kTemp);
                    }
                }
                Set(std::move(parsed), now);
            }
        }
#else
        (void)refresh;
#endif
    });
}

VersionsList::~VersionsList() {
    m_stop = true; // a download going on stops
    if (m_thread.joinable()) m_thread.join();
}

} // namespace nxdock
