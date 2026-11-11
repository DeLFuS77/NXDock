// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "tls_pin.hpp"
#include "speedtest.hpp"

#include <switch.h>
#include <curl/curl.h>
#include <sys/socket.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include "i18n.hpp"
#include "errors.hpp"
#include "i18n_more.hpp"
#include "log.hpp"
#include "net.hpp"

namespace nxdock {
namespace {

using Clock = std::chrono::steady_clock;

constexpr long long kChunk = 8ll << 20;          // one ranged request
constexpr long long kVirtualSize = 8ll << 30;    // the size asked of the server
constexpr double kWarmup = 1.0;                  // seconds left out while TCP speeds up
constexpr double kMeasure = 7.0;                 // seconds measured
constexpr long long kSdBytes = 256ll << 20;
constexpr long long kSdBlock = 4ll << 20;

double Seconds(Clock::time_point from) {
    return std::chrono::duration<double>(Clock::now() - from).count();
}

struct Worker {
    std::atomic<long long>* next;
    std::atomic<long long>* bytes;
    std::atomic<bool>* stop;
    std::atomic<long>* status; // first HTTP status that was not a success
    std::string error;
};

size_t OnData(char*, size_t size, size_t count, void* user) {
    auto* w = static_cast<Worker*>(user);
    if (w->stop->load(std::memory_order_relaxed)) {
        return 0; // ends the transfer
    }
    w->bytes->fetch_add(static_cast<long long>(size * count), std::memory_order_relaxed);
    return size * count;
}

int OnSocket(void*, curl_socket_t fd, curlsocktype) {
    // Ask for the large buffer the mode allows; the system grows to it as the data flows.
    const int size = net::ReceiveBuffer();
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    return CURL_SOCKOPT_OK;
}

void Download(Worker* w, std::string url, std::string key) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        w->error = "curl";
        return;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    tls::Apply(curl, url);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, "nxdock");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, key.c_str());
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, long(CURL_MAX_READ_SIZE));
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, OnSocket);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, w);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXDock/" NXDOCK_VERSION);

    while (!w->stop->load()) {
        const long long from = w->next->fetch_add(kChunk) % (kVirtualSize - kChunk);
        char range[64];
        std::snprintf(range, sizeof(range), "%lld-%lld", from, from + kChunk - 1);
        curl_easy_setopt(curl, CURLOPT_RANGE, range);
        const CURLcode res = curl_easy_perform(curl);
        if (w->stop->load()) {
            break;
        }
        if (res != CURLE_OK) {
            w->error = curl_easy_strerror(res);
            break;
        }
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (code != 206 && code != 200) {
            long expected = 0;
            w->status->compare_exchange_strong(expected, code);
            break;
        }
    }
    curl_easy_cleanup(curl);
}

} // namespace

SpeedTest::~SpeedTest() {
    Cancel();
}

void SpeedTest::Start(const std::string& url, const std::string& key) {
    if (m_running.load()) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    {
        std::lock_guard lock(m_mutex);
        m_view = View{};
    }
    m_cancel = false;
    m_running = true;
    m_thread = std::thread(&SpeedTest::Run, this, url, key);
}

void SpeedTest::Cancel() {
    m_cancel = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

SpeedTest::View SpeedTest::Snapshot() {
    std::lock_guard lock(m_mutex);
    return m_view;
}

void SpeedTest::Set(Stage stage, float progress, double live) {
    std::lock_guard lock(m_mutex);
    m_view.stage = stage;
    m_view.progress = progress;
    m_view.live = live;
}

void SpeedTest::Fail(const std::string& error) {
    std::lock_guard lock(m_mutex);
    m_view.stage = Stage::Failed;
    m_view.error = error;
}

void SpeedTest::Run(std::string url, std::string key) {
    net::Boost boost;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    const std::string target = url + "/install/speedtest?size=" + std::to_string(kVirtualSize);

    const struct { int connections; Stage stage; } stages[] = {{1, Stage::Net1}, {2, Stage::Net2}, {4, Stage::Net4}};
    for (int i = 0; i < 3 && !m_cancel; i++) {
        const double mbps = NetStage(target, key, stages[i].connections, stages[i].stage, i);
        if (mbps < 0) {
            if (m_cancel) Set(Stage::Idle, 0, 0);
            m_running = false;
            return;
        }
        std::lock_guard lock(m_mutex);
        m_view.net[i] = mbps;
    }
    if (!m_cancel) {
        const double sd = SdStage();
        if (sd < 0) {
            if (m_cancel) Set(Stage::Idle, 0, 0);
            m_running = false;
            return;
        }
        std::lock_guard lock(m_mutex);
        m_view.sd = sd;
    }
    Set(m_cancel ? Stage::Idle : Stage::Done, 1, 0);
    m_running = false;
}

double SpeedTest::NetStage(const std::string& url, const std::string& key, int connections, Stage stage, int) {
    std::atomic<long long> next{0}, bytes{0};
    std::atomic<bool> stop{false};
    std::atomic<long> status{0};
    std::vector<Worker> workers(connections);
    std::vector<std::thread> threads;
    for (int i = 0; i < connections; i++) {
        workers[i] = Worker{&next, &bytes, &stop, &status, {}};
        threads.emplace_back(Download, &workers[i], url, key);
    }

    const auto start = Clock::now();
    long long base = 0;
    bool measuring = false;
    Clock::time_point from = start;
    double result = 0;
    while (!m_cancel) {
        svcSleepThread(100'000'000);
        const double t = Seconds(start);
        bool failed = status.load() != 0;
        for (auto& w : workers) {
            failed = failed || !w.error.empty();
        }
        if (failed) {
            break;
        }
        if (!measuring && t >= kWarmup) {
            measuring = true;
            base = bytes.load();
            from = Clock::now();
        }
        const double span = measuring ? Seconds(from) : t;
        const double got = double(bytes.load() - (measuring ? base : 0));
        const double live = span > 0 ? got / span / (1 << 20) : 0;
        Set(stage, float(std::min(1.0, t / (kWarmup + kMeasure))), live);
        if (measuring && span >= kMeasure) {
            result = live;
            break;
        }
    }
    stop = true;
    for (auto& th : threads) {
        th.join();
    }
    if (m_cancel) {
        return -1;
    }

    const auto& s = Tr();
    if (const long code = status.load()) {
        if (code == 401) Fail(s.err_key);
        else if (code == 404) Fail(s.err_off);
        else if (code == 403) Fail(s.err_forbidden);
        else Fail(PackError({s.err_connect, "", TrM().sp_todo, "HTTP " + std::to_string(code)}));
        return -1;
    }
    for (auto& w : workers) {
        if (!w.error.empty()) {
            // the code of curl only in the small line, and in install.log
            log_write("[NXDock] speed test: %s\n", w.error.c_str());
            Fail(PackError({s.err_connect, "", TrM().sp_todo, w.error}));
            return -1;
        }
    }
    return result;
}

double SpeedTest::SdStage() {
    const auto& s = Tr();
    FsFileSystem fs;
    if (R_FAILED(fsOpenSdCardFileSystem(&fs))) {
        Fail(s.err_sd);
        return -1;
    }
    char path[FS_MAX_PATH] = "/switch/NXDock/speedtest.tmp";
    char dir[FS_MAX_PATH] = "/switch/NXDock";
    fsFsCreateDirectory(&fs, dir);
    fsFsDeleteFile(&fs, path);
    double result = -1;
    FsFile file;
    if (R_SUCCEEDED(fsFsCreateFile(&fs, path, kSdBytes, 0)) &&
        R_SUCCEEDED(fsFsOpenFile(&fs, path, FsOpenMode_Write, &file))) {
        std::vector<u8> block(kSdBlock);
        for (size_t i = 0; i < block.size(); i++) {
            block[i] = u8(i * 2654435761u >> 24);
        }
        const auto start = Clock::now();
        long long written = 0;
        bool ok = true;
        while (written < kSdBytes && !m_cancel) {
            if (R_FAILED(fsFileWrite(&file, written, block.data(), block.size(), FsWriteOption_None))) {
                ok = false;
                break;
            }
            written += kSdBlock;
            const double t = Seconds(start);
            Set(Stage::Sd, float(double(written) / kSdBytes), t > 0 ? written / t / (1 << 20) : 0);
        }
        if (ok) {
            fsFileFlush(&file);
        }
        const double t = Seconds(start);
        fsFileClose(&file);
        if (ok && !m_cancel && t > 0) {
            result = written / t / (1 << 20);
        } else if (!ok) {
            Fail(s.err_sd);
        }
    } else {
        Fail(s.err_sd);
    }
    fsFsDeleteFile(&fs, path);
    fsFsClose(&fs);
    return m_cancel ? -1 : result;
}

} // namespace nxdock
