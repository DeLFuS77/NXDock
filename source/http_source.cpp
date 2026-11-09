// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "http_source.hpp"
#include "log.hpp"

#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <strings.h>

namespace nxdock {
namespace {

constexpr size_t kSmallBlock = 256 * 1024;

using Clock = std::chrono::steady_clock;

struct Transfer {
    unsigned char* dst = nullptr;
    size_t size = 0;
    size_t got = 0;
    s64 off = 0;
    long status = 0;
    s64 total = -1; // from Content-Range
    bool refused = false;
    // when a piece is fetched: tell its reader as bytes arrive
    std::function<bool(size_t)> on_bytes;
    std::function<bool()> stop;
};

size_t OnHeader(char* data, size_t size, size_t count, void* user) {
    auto* t = static_cast<Transfer*>(user);
    const size_t n = size * count;
    std::string line(data, n);
    if (line.rfind("HTTP/", 0) == 0) {
        // a new response (after a redirect, or 100 Continue): start over
        const size_t sp = line.find(' ');
        t->status = sp == std::string::npos ? 0 : std::strtol(line.c_str() + sp + 1, nullptr, 10);
        t->total = -1;
    } else if (line.size() > 14 && strncasecmp(line.c_str(), "Content-Range:", 14) == 0) {
        const size_t slash = line.find('/');
        if (slash != std::string::npos && line[slash + 1] != '*') {
            t->total = std::strtoll(line.c_str() + slash + 1, nullptr, 10);
        }
    }
    return n;
}

size_t OnBody(char* data, size_t size, size_t count, void* user) {
    auto* t = static_cast<Transfer*>(user);
    const size_t n = size * count;
    // a server that ignores Range sends the file from its start: never take that for the piece
    if (t->status != 206 && !(t->status == 200 && t->off == 0)) {
        t->refused = t->status == 200;
        return 0;
    }
    if (t->got + n > t->size || (t->stop && t->stop())) {
        return 0;
    }
    std::memcpy(t->dst + t->got, data, n);
    t->got += n;
    if (t->on_bytes && !t->on_bytes(n)) {
        return 0;
    }
    return n;
}

int OnProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* t = static_cast<Transfer*>(user);
    return t->stop && t->stop() ? 1 : 0;
}

CURL* NewHandle(const std::string& url, const std::string& key) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return nullptr;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXDock/" NXDOCK_VERSION);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    // a connection that stalls is dropped and asked again from where it stopped
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 512L * 1024);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, OnHeader);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    if (!key.empty()) {
        // as http.cpp: the key is the password of basic authentication
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERNAME, "nxdock");
        curl_easy_setopt(curl, CURLOPT_PASSWORD, key.c_str());
    }
    return curl;
}

} // namespace

int ConnectionsFor(int tested_best, bool applet) {
    int n = tested_best == 1 || tested_best == 2 || tested_best == 4 ? tested_best : 4;
    return applet ? std::min(n, 2) : n;
}

HttpSource::HttpSource(std::string url, std::string key, Options options, sphaira::ui::ProgressBox* progress)
    : m_url(std::move(url)), m_key(std::move(key)), m_opt(options), m_progress(progress) {
    m_opt.connections = std::clamp(m_opt.connections, 1, 8);
    m_opt.ahead = std::max(1, m_opt.ahead);
    m_opt.piece = std::max<size_t>(m_opt.piece, 256 * 1024);
    for (int i = 0; i < m_opt.connections; i++) {
        m_workers.emplace_back([this] { Worker(); });
    }
}

HttpSource::~HttpSource() {
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
        for (auto& [i, p] : m_pieces) {
            p->dropped = true;
        }
    }
    m_cv.notify_all();
    m_filled.notify_all();
    for (auto& t : m_workers) {
        t.join();
    }
    if (m_direct) {
        curl_easy_cleanup(static_cast<CURL*>(m_direct));
    }
}

bool HttpSource::Stopping() const {
    return m_quit || (m_progress && m_progress->Cancelled());
}

void HttpSource::SignalCancel() {
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_cv.notify_all();
    m_filled.notify_all();
}

Result HttpSource::Open() {
    if (!m_direct) {
        m_direct = NewHandle(m_url, m_key);
        R_UNLESS(m_direct, Result_OutOfMemory);
    }
    unsigned char byte;
    size_t got = 0;
    bool retry = false;
    for (int attempt = 0; attempt < 4; attempt++) {
        const Result rc = FetchOnce(m_direct, &byte, 0, 1, got, nullptr, retry);
        if (R_SUCCEEDED(rc) || !retry) {
            R_TRY(rc);
            break;
        }
        svcSleepThread(500'000'000LL * (attempt + 1));
    }
    R_UNLESS(m_size > 0, Result_ServerRefused);
    log_write("[http] %s: %lld bytes, %d connections, pieces of %zu\n", "file", (long long)m_size, m_opt.connections, m_opt.piece);
    R_SUCCEED();
}

Result HttpSource::FetchOnce(void* handle, unsigned char* dst, s64 off, size_t size, size_t& got, Piece* piece, bool& retry) {
    CURL* curl = static_cast<CURL*>(handle);
    retry = false;
    got = 0;
    Transfer t;
    t.dst = dst;
    t.size = size;
    t.off = off;
    t.stop = [this, piece] { return Stopping() || (piece && piece->dropped); };
    t.on_bytes = [this, piece](size_t n) {
        if (m_progress) {
            m_progress->AddNet(n);
        }
        if (piece) {
            std::lock_guard lock(m_mutex);
            piece->filled += n;
            m_filled.notify_all();
        }
        return true;
    };
    char range[64];
    std::snprintf(range, sizeof(range), "%lld-%lld", (long long)off, (long long)(off + s64(size) - 1));
    curl_easy_setopt(curl, CURLOPT_RANGE, range);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &t);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);
    const CURLcode res = curl_easy_perform(curl);
    got = t.got;
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    if (t.total > 0) {
        if (m_size < 0) {
            m_size = t.total;
        } else if (t.total != m_size) {
            log_write("[http] the file changed: %lld, was %lld\n", (long long)t.total, (long long)m_size);
            R_THROW(Result_ServerChanged);
        }
    }
    if (Stopping() || (piece && piece->dropped)) {
        R_THROW(Result_TransferCancelled);
    }
    if (status == 401 || status == 403 || status == 404 || status == 410 || t.refused) {
        log_write("[http] refused: %ld\n", status);
        R_THROW(Result_ServerRefused);
    }
    if (status == 416) {
        R_THROW(Result_ServerChanged);
    }
    if (res == CURLE_OK && (status == 206 || status == 200) && got == size) {
        R_SUCCEED();
    }
    log_write("[http] %s at %lld: got %zu of %zu, status %ld; again\n", curl_easy_strerror(res), (long long)off, got, size, status);
    retry = true;
    R_THROW(Result_NetworkLost);
}

Result HttpSource::Fetch(void* curl, unsigned char* dst, s64 off, size_t size, Piece* piece) {
    auto last_progress = Clock::now();
    int attempt = 0;
    while (size > 0) {
        size_t got = 0;
        bool retry = false;
        const Result rc = FetchOnce(curl, dst, off, size, got, piece, retry);
        if (R_SUCCEEDED(rc)) {
            R_SUCCEED();
        }
        if (!retry) {
            return rc;
        }
        // keep what arrived, ask for the rest
        dst += got;
        off += s64(got);
        size -= got;
        if (got > 0) {
            last_progress = Clock::now();
            attempt = 0;
        }
        if (Clock::now() - last_progress > std::chrono::seconds(m_opt.give_up_seconds)) {
            R_THROW(Result_NetworkLost);
        }
        // 0.25 s, 0.5, 1, 2, then 3 s between tries while the network is away
        const s64 wait_ms = std::min<s64>(250LL << std::min(attempt, 3), 3000);
        attempt++;
        for (s64 waited = 0; waited < wait_ms; waited += 50) {
            if (Stopping() || (piece && piece->dropped)) {
                R_THROW(Result_TransferCancelled);
            }
            svcSleepThread(50'000'000LL);
        }
    }
    R_SUCCEED();
}

HttpSource::PiecePtr HttpSource::NextQueued() {
    for (auto& [index, piece] : m_pieces) {
        if (piece->state == Piece::State::Queued) {
            return piece;
        }
    }
    return nullptr;
}

void HttpSource::Worker() {
    CURL* curl = NewHandle(m_url, m_key);
    std::unique_lock lock(m_mutex);
    while (!m_quit) {
        PiecePtr piece = NextQueued();
        if (!piece) {
            m_cv.wait(lock);
            continue;
        }
        piece->state = Piece::State::Fetching;
        const s64 off = m_hint_off + piece->index * s64(m_opt.piece);
        lock.unlock();
        Result rc = curl ? Result(0) : Result(Result_OutOfMemory);
        if (R_SUCCEEDED(rc)) {
            try {
                piece->data.resize(piece->want);
            } catch (...) {
                rc = Result_OutOfMemory;
            }
        }
        if (R_SUCCEEDED(rc)) {
            rc = Fetch(curl, piece->data.data(), off, piece->want, piece.get());
        }
        lock.lock();
        piece->rc = rc;
        piece->state = R_SUCCEEDED(rc) ? Piece::State::Ready : Piece::State::Failed;
        m_filled.notify_all();
    }
    lock.unlock();
    if (curl) {
        curl_easy_cleanup(curl);
    }
}

void HttpSource::HintRange(s64 off, s64 size) {
    std::lock_guard lock(m_mutex);
    for (auto& [i, p] : m_pieces) {
        p->dropped = true;
    }
    m_pieces.clear();
    m_hint_off = off;
    m_hint_end = off + size;
}

// With the lock held: the pieces from first on, up to the depth, are queued; those before it go.
void HttpSource::Schedule(s64 first) {
    for (auto it = m_pieces.begin(); it != m_pieces.end();) {
        if (it->first < first) {
            it->second->dropped = true;
            it = m_pieces.erase(it);
        } else {
            ++it;
        }
    }
    const s64 depth = s64(m_opt.connections) * m_opt.ahead;
    bool added = false;
    for (s64 i = first; i < first + depth; i++) {
        const s64 start = m_hint_off + i * s64(m_opt.piece);
        if (start >= m_hint_end) {
            break;
        }
        if (!m_pieces.count(i)) {
            auto p = std::make_shared<Piece>();
            p->index = i;
            p->want = size_t(std::min<s64>(s64(m_opt.piece), m_hint_end - start));
            m_pieces[i] = p;
            added = true;
        }
    }
    if (added) {
        m_cv.notify_all();
    }
}

Result HttpSource::ReadDirect(void* buf, s64 off, s64 size) {
    if (!m_direct) {
        m_direct = NewHandle(m_url, m_key);
        R_UNLESS(m_direct, Result_OutOfMemory);
    }
    if (size > s64(kSmallBlock)) {
        return Fetch(m_direct, static_cast<unsigned char*>(buf), off, size_t(size), nullptr);
    }
    // small reads near each other (the PFS0 header, then its names) come from one block
    if (m_small_off < 0 || off < m_small_off || off + size > m_small_off + s64(m_small.size())) {
        const s64 want = std::min<s64>(kSmallBlock, m_size - off);
        R_UNLESS(want >= size, Result_YatiHttpReadFailed);
        m_small.resize(size_t(want));
        m_small_off = -1;
        R_TRY(Fetch(m_direct, m_small.data(), off, size_t(want), nullptr));
        m_small_off = off;
    }
    std::memcpy(buf, m_small.data() + (off - m_small_off), size_t(size));
    R_SUCCEED();
}

Result HttpSource::Read(void* buf, s64 off, s64 size, u64* bytes_read) {
    *bytes_read = 0;
    R_UNLESS(m_size >= 0, Result_YatiHttpReadFailed);
    R_UNLESS(off >= 0 && off <= m_size, Result_YatiHttpReadFailed);
    size = std::min(size, m_size - off);
    if (size <= 0) {
        R_SUCCEED();
    }
    R_UNLESS(!Stopping(), Result_TransferCancelled);

    bool in_hint;
    {
        std::lock_guard lock(m_mutex);
        in_hint = m_hint_end > m_hint_off && off >= m_hint_off && off + size <= m_hint_end;
    }
    if (!in_hint) {
        R_TRY(ReadDirect(buf, off, size));
        *bytes_read = u64(size);
        R_SUCCEED();
    }

    auto* out = static_cast<unsigned char*>(buf);
    s64 done = 0;
    std::unique_lock lock(m_mutex);
    while (done < size) {
        const s64 pos = off + done;
        const s64 index = (pos - m_hint_off) / s64(m_opt.piece);
        Schedule(index);
        PiecePtr piece = m_pieces[index];
        const size_t start = size_t(pos - (m_hint_off + index * s64(m_opt.piece)));
        const size_t need = size_t(std::min<s64>(size - done, s64(piece->want - start)));
        // copy as soon as the bytes needed are there, not when the whole piece is
        m_filled.wait(lock, [&] {
            return Stopping() || piece->state == Piece::State::Failed || piece->filled >= start + need;
        });
        if (piece->state == Piece::State::Failed) {
            return piece->rc;
        }
        R_UNLESS(!Stopping(), Result_TransferCancelled);
        std::memcpy(out + done, piece->data.data() + start, need);
        done += s64(need);
    }
    *bytes_read = u64(size);
    R_SUCCEED();
}

} // namespace nxdock
