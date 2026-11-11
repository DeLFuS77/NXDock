// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "updater.hpp"

#include "minisig.hpp"

#include <curl/curl.h>
#include <mbedtls/sha256.h>
#include <yyjson.h>

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <vector>

#ifndef NXDOCK_RELEASE_KEYS
#define NXDOCK_RELEASE_KEYS ""
#endif
#ifndef NXDOCK_VERSION
#define NXDOCK_VERSION "0.0.0"
#endif

namespace nxdock {
namespace {

constexpr const char* kLatest = "https://api.github.com/repos/DeLFuS77/NXDock/releases/latest";

struct Sink {
    std::string* body;
    size_t max;
    std::atomic<unsigned long long>* done;
};

size_t OnBody(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<Sink*>(user);
    const size_t n = size * count;
    if (sink->body->size() + n > sink->max) return 0; // too large: stopped
    sink->body->append(data, n);
    if (sink->done) *sink->done = sink->body->size();
    return n;
}

// A GET that follows GitHub's redirects to its downloads. Gives the HTTP status, 0 without answer.
long Fetch(const std::string& url, std::string& body, size_t max, std::atomic<unsigned long long>* done = nullptr) {
    body.clear();
    CURL* curl = curl_easy_init();
    if (!curl) return 0;
    Sink sink{&body, max, done};
    curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, long(CURLPROTO_HTTPS));
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, long(CURLPROTO_HTTPS));
    // no authority to check certificates with: what is downloaded is checked by its signature
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXDock/" NXDOCK_VERSION);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 8000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    long status = 0;
    if (curl_easy_perform(curl) == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

std::vector<int> Parts(std::string v) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    std::vector<int> out;
    size_t i = 0;
    while (i < v.size() && out.size() < 4) {
        if (v[i] < '0' || v[i] > '9') break; // "-beta.1" and the rest are left out
        out.push_back(std::atoi(v.c_str() + i));
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') i++;
        if (i < v.size() && v[i] == '.') i++;
        else break;
    }
    while (out.size() < 3) out.push_back(0);
    return out;
}

std::string Hex(const unsigned char* data, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; i++) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

bool Exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

} // namespace

Updater::~Updater() { Join(); }

void Updater::Join() {
    if (m_thread.joinable()) m_thread.join();
}

void Updater::Set(Stage stage, Error error) {
    std::lock_guard lock(m_mutex);
    m_view.stage = stage;
    m_view.error = error;
}

Updater::View Updater::Snapshot() {
    std::lock_guard lock(m_mutex);
    View v = m_view;
    v.done = m_done;
    return v;
}

bool Updater::HasKeys() { return !minisig::ParseKeys(NXDOCK_RELEASE_KEYS).empty(); }

bool Updater::Newer(const std::string& a, const std::string& b) { return Parts(a) > Parts(b); }

bool Updater::HasOld(const std::string& nro) { return !nro.empty() && Exists(nro + ".old"); }

bool Updater::RollBack(const std::string& nro) {
    if (!HasOld(nro)) return false;
    const std::string mid = nro + ".swap";
    std::remove(mid.c_str());
    if (std::rename(nro.c_str(), mid.c_str()) != 0) return false;
    if (std::rename((nro + ".old").c_str(), nro.c_str()) != 0) {
        std::rename(mid.c_str(), nro.c_str()); // as it was
        return false;
    }
    std::rename(mid.c_str(), (nro + ".old").c_str()); // the newer one kept in its place, to go forward again
    return true;
}

std::string Updater::Notes(const std::string& body, int lines) {
    std::string out;
    int count = 0;
    size_t at = 0;
    while (at < body.size() && count < lines) {
        size_t end = body.find('\n', at);
        if (end == std::string::npos) end = body.size();
        std::string line = body.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("> ", 0) == 0 || line.rfind("###", 0) == 0) break; // the notice and the downloads after the news
        if (line.rfind("- ", 0) != 0 && line.rfind("* ", 0) != 0) continue;
        line.erase(0, 2);
        std::string clean;
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == '*' || line[i] == '`') continue;
            clean += line[i];
        }
        if (clean.size() > 110) {
            size_t cut = 107;
            while (cut > 0 && (static_cast<unsigned char>(clean[cut]) & 0xC0) == 0x80) cut--;
            clean = clean.substr(0, cut) + "…";
        }
        if (!out.empty()) out += "\n";
        out += "· " + clean;
        count++;
    }
    return out;
}

std::string Updater::SumOf(const std::string& sums, const std::string& name) {
    size_t at = 0;
    while (at < sums.size()) {
        size_t end = sums.find('\n', at);
        if (end == std::string::npos) end = sums.size();
        std::string line = sums.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // "<64 hex>  name" (or " *name" for binary mode)
        if (line.size() < 66) continue;
        std::string file = line.substr(64);
        while (!file.empty() && (file[0] == ' ' || file[0] == '*')) file.erase(0, 1);
        if (file == name) return line.substr(0, 64);
    }
    return {};
}

void Updater::Check() {
    {
        std::lock_guard lock(m_mutex);
        if (m_view.stage == Stage::Checking || m_view.stage == Stage::Downloading) return;
    }
    Join();
    Set(Stage::Checking);
    m_thread = std::thread([this] {
        std::string body;
        const long status = Fetch(kLatest, body, 512 * 1024);
        if (status == 0) return Set(Stage::Failed, Error::Network);
        if (status != 200) return Set(Stage::Failed, Error::NoRelease); // none published yet (or the repository is private)
        yyjson_doc* doc = yyjson_read(body.data(), body.size(), 0);
        yyjson_val* root = doc ? yyjson_doc_get_root(doc) : nullptr;
        const char* tag = root ? yyjson_get_str(yyjson_obj_get(root, "tag_name")) : nullptr;
        const char* notes = root ? yyjson_get_str(yyjson_obj_get(root, "body")) : nullptr;
        std::string nro, sums, sig;
        unsigned long long size = 0;
        yyjson_val* assets = root ? yyjson_obj_get(root, "assets") : nullptr;
        size_t i, n;
        yyjson_val* a;
        yyjson_arr_foreach(assets, i, n, a) {
            const char* name = yyjson_get_str(yyjson_obj_get(a, "name"));
            const char* url = yyjson_get_str(yyjson_obj_get(a, "browser_download_url"));
            if (!name || !url) continue;
            const std::string nm = name;
            if (nm == "NXDock.nro") {
                nro = url;
                size = yyjson_get_uint(yyjson_obj_get(a, "size"));
            } else if (nm == "SHA256SUMS.txt") {
                sums = url;
            } else if (nm == "SHA256SUMS.txt.minisig") {
                sig = url;
            }
        }
        const std::string version = tag ? tag : "";
        const std::string news = notes ? Notes(notes) : "";
        if (doc) yyjson_doc_free(doc);
        if (version.empty() || nro.empty() || sums.empty() || sig.empty()) return Set(Stage::Failed, Error::NoRelease);
        std::lock_guard lock(m_mutex);
        m_view.version = version;
        m_view.notes = news;
        m_view.size = size;
        m_nro_url = nro;
        m_sums_url = sums;
        m_sig_url = sig;
        m_view.error = Error::None;
        m_view.stage = Newer(version, NXDOCK_VERSION) ? Stage::Newer : Stage::Latest;
    });
}

void Updater::Install(const std::string& target) {
    std::string version, nro_url, sums_url, sig_url;
    {
        std::lock_guard lock(m_mutex);
        if (m_view.stage != Stage::Newer) return;
        version = m_view.version;
        nro_url = m_nro_url;
        sums_url = m_sums_url;
        sig_url = m_sig_url;
        m_view.stage = Stage::Downloading;
    }
    Join();
    m_done = 0;
    m_thread = std::thread([this, version, nro_url, sums_url, sig_url, target] {
        const auto keys = minisig::ParseKeys(NXDOCK_RELEASE_KEYS);
        if (keys.empty()) return Set(Stage::Failed, Error::NoKeys);
        std::string sums, sig, nro;
        if (Fetch(sums_url, sums, 64 * 1024) != 200 || Fetch(sig_url, sig, 8 * 1024) != 200) return Set(Stage::Failed, Error::Network);
        // the checksums signed with a key of the project, for this very version
        std::string comment;
        if (!minisig::Verify(sums, sig, keys, comment) || comment != "NXDock " + version) return Set(Stage::Failed, Error::Signature);
        const std::string want = SumOf(sums, "NXDock.nro");
        if (want.empty()) return Set(Stage::Failed, Error::Signature);
        if (Fetch(nro_url, nro, 64u << 20, &m_done) != 200) return Set(Stage::Failed, Error::Network);
        unsigned char sum[32];
        mbedtls_sha256(reinterpret_cast<const unsigned char*>(nro.data()), nro.size(), sum, 0);
        if (Hex(sum, sizeof(sum)) != want) return Set(Stage::Failed, Error::Sum);
        // written beside it, then swapped: the one running is kept as .old
        const std::string fresh = target + ".new", old = target + ".old";
        FILE* f = std::fopen(fresh.c_str(), "wb");
        if (!f) return Set(Stage::Failed, Error::Write);
        const bool written = std::fwrite(nro.data(), 1, nro.size(), f) == nro.size();
        if (std::fclose(f) != 0 || !written) {
            std::remove(fresh.c_str());
            return Set(Stage::Failed, Error::Write);
        }
        std::remove(old.c_str());
        if (std::rename(target.c_str(), old.c_str()) != 0) {
            std::remove(fresh.c_str());
            return Set(Stage::Failed, Error::Write);
        }
        if (std::rename(fresh.c_str(), target.c_str()) != 0) {
            std::rename(old.c_str(), target.c_str()); // the one running, back in place
            std::remove(fresh.c_str());
            return Set(Stage::Failed, Error::Write);
        }
        Set(Stage::Done);
    });
}

} // namespace nxdock
