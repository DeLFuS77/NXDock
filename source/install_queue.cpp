// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "install_queue.hpp"

#include "http_source.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "ui/progress_box.hpp"
#include "yati/container/nsp.hpp"
#include "yati/yati.hpp"
#include "log.hpp"

#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" void nxdock_log_open(const char* path);
extern "C" void nxdock_log_close();

namespace nxdock {
namespace {

using sphaira::ui::ProgressBox;

double Now() {
    return double(armTicksToNs(armGetSystemTick())) / 1e9;
}

std::string Size(u64 bytes) {
    char t[32];
    if (bytes >= 1024ULL * 1024 * 1024) {
        std::snprintf(t, sizeof(t), "%.1f GB", double(bytes) / (1024.0 * 1024 * 1024));
    } else {
        std::snprintf(t, sizeof(t), "%.0f MB", double(bytes) / (1024.0 * 1024));
    }
    return t;
}

// The download-only check, for an emulator that cannot install: every NCA of the NSP is read
// through the same source and its SHA-256 compared with the content id in its name. Nothing is
// written. Turned on with download_test=1 in config.ini.
Result DownloadCheck(HttpSource& source, ProgressBox& box) {
    sphaira::yati::container::Nsp nsp(&source);
    sphaira::yati::container::Collections collections;
    R_TRY(nsp.GetCollections(collections));
    u64 total = 0;
    u32 count = 0;
    for (const auto& c : collections) {
        if (c.name.ends_with(".nca") || c.name.ends_with(".ncz")) {
            total += u64(c.size);
            count++;
        }
    }
    box.SetPlan(total, count);
    box.SetPhase(ProgressBox::Phase::Installing);
    box.SetThreads(0);
    std::vector<u8> buf(8 * 1024 * 1024);
    for (const auto& c : collections) {
        const bool nca = c.name.ends_with(".nca");
        if (!nca && !c.name.ends_with(".ncz")) {
            continue;
        }
        box.NewTransfer(c.name);
        source.HintRange(c.offset, c.size);
        Sha256Context sha;
        sha256ContextCreate(&sha);
        for (s64 off = 0; off < c.size;) {
            const s64 n = std::min<s64>(s64(buf.size()), c.size - off);
            u64 got = 0;
            R_TRY(source.Read(buf.data(), c.offset + off, n, &got));
            R_UNLESS(s64(got) == n, Result_YatiInvalidNcaReadSize);
            sha256ContextUpdate(&sha, buf.data(), size_t(n));
            box.AddReady(u64(n));
            off += n;
            box.UpdateTransfer(off, c.size);
        }
        u8 hash[SHA256_HASH_SIZE];
        sha256ContextGetHash(&sha, hash);
        if (nca && c.name.size() >= 32) {
            u8 id[16];
            for (int i = 0; i < 16; i++) {
                char hex[3] = {c.name[i * 2], c.name[i * 2 + 1], 0};
                id[i] = u8(std::strtoul(hex, nullptr, 16));
            }
            log_write("[check] %s: %s\n", c.name.c_str(), std::memcmp(id, hash, 16) ? "BAD" : "ok");
            R_UNLESS(!std::memcmp(id, hash, 16), Result_YatiInvalidNcaSha256);
        }
        box.NcaChecked();
    }
    R_SUCCEED();
}

} // namespace

std::string InstallErrorText(unsigned rc, unsigned long long needed, unsigned firmware, long long free_bytes) {
    const auto& s = Tr();
    char text[256];
    const u32 desc = R_DESCRIPTION(rc);
    if (R_MODULE(rc) == Module_NXDock) {
        switch (NXDockResult(desc)) {
            case NXDockResult::TransferCancelled: return s.err_cancelled;
            case NXDockResult::NotEnoughSpace:
                std::snprintf(text, sizeof(text), s.err_space, Size(needed).c_str(), free_bytes >= 0 ? Size(u64(free_bytes)).c_str() : "?");
                return text;
            case NXDockResult::FirmwareTooOld: {
                char fw[24];
                std::snprintf(fw, sizeof(fw), "%u.%u.%u", firmware >> 26, (firmware >> 20) & 0x3F, (firmware >> 16) & 0xF);
                std::snprintf(text, sizeof(text), s.err_firmware, fw);
                return text;
            }
            case NXDockResult::NetworkLost: return s.err_network;
            case NXDockResult::YatiHttpReadFailed: return s.err_network;
            case NXDockResult::ServerRefused: return s.err_refused;
            case NXDockResult::ServerChanged: return s.err_changed;
            case NXDockResult::YatiInvalidNcaSha256: return s.err_sha;
            case NXDockResult::YatiNczSectionNotFound:
            case NXDockResult::YatiInvalidNczSectionCount:
            case NXDockResult::YatiNczBlockNotFound:
            case NXDockResult::YatiInvalidNczBlockVersion:
            case NXDockResult::YatiInvalidNczBlockType:
            case NXDockResult::YatiInvalidNczBlockTotal:
            case NXDockResult::YatiInvalidNczBlockSizeExponent:
            case NXDockResult::YatiInvalidNczZstdError: return s.err_ncz;
            case NXDockResult::YatiTicketNotFound:
            case NXDockResult::YatiCertNotFound: return s.err_ticket;
            case NXDockResult::OutOfMemory: return s.err_memory;
            case NXDockResult::NczExperimental: return s.err_experimental;
            default: break;
        }
    }
    std::snprintf(text, sizeof(text), s.err_other, 2000 + R_MODULE(rc), desc);
    return text;
}

InstallQueue::InstallQueue() {
    m_thread = std::thread([this] { Run(); });
}

InstallQueue::~InstallQueue() {
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
        if (m_box) {
            m_box->RequestCancel();
        }
    }
    m_cv.notify_all();
    m_thread.join();
}

void InstallQueue::SetOptions(int connections, bool download_test) {
    std::lock_guard lock(m_mutex);
    m_connections = connections;
    m_download_test = download_test;
}

bool InstallQueue::Add(InstallJob job) {
    {
        std::lock_guard lock(m_mutex);
        for (const auto& j : m_jobs) {
            if (j.file_id == job.file_id && (j.state == InstallJob::State::Waiting || j.state == InstallJob::State::Running)) {
                return false;
            }
        }
        job.id = m_next_id++;
        job.state = InstallJob::State::Waiting;
        m_jobs.push_back(std::move(job));
    }
    m_cv.notify_all();
    return true;
}

void InstallQueue::Remove(int id) {
    std::lock_guard lock(m_mutex);
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        if (it->id != id) {
            continue;
        }
        if (it->state == InstallJob::State::Running) {
            if (m_box) {
                m_box->RequestCancel();
            }
        } else {
            m_jobs.erase(it);
        }
        return;
    }
}

void InstallQueue::Retry(int id) {
    {
        std::lock_guard lock(m_mutex);
        auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [id](const InstallJob& j) { return j.id == id; });
        if (it == m_jobs.end() || (it->state != InstallJob::State::Failed && it->state != InstallJob::State::Cancelled)) {
            return;
        }
        InstallJob again = *it;
        m_jobs.erase(it);
        again.id = m_next_id++;
        again.state = InstallJob::State::Waiting;
        again.error.clear();
        m_jobs.push_back(std::move(again));
    }
    m_cv.notify_all();
}

void InstallQueue::Clear() {
    std::lock_guard lock(m_mutex);
    std::erase_if(m_jobs, [](const InstallJob& j) {
        return j.state != InstallJob::State::Waiting && j.state != InstallJob::State::Running;
    });
}

std::vector<InstallJob> InstallQueue::Jobs() {
    std::lock_guard lock(m_mutex);
    std::vector<InstallJob> out = m_jobs;
    for (auto& j : out) {
        j.key.clear(); // the key stays here
    }
    return out;
}

bool InstallQueue::Busy() {
    std::lock_guard lock(m_mutex);
    return m_running != 0 || std::any_of(m_jobs.begin(), m_jobs.end(), [](const InstallJob& j) { return j.state == InstallJob::State::Waiting; });
}

int InstallQueue::Waiting() {
    std::lock_guard lock(m_mutex);
    return int(std::count_if(m_jobs.begin(), m_jobs.end(), [](const InstallJob& j) { return j.state == InstallJob::State::Waiting; }));
}

std::map<u64, InstalledState> InstallQueue::Installed() {
    std::lock_guard lock(m_mutex);
    return m_installed;
}

InstallQueue::Live InstallQueue::GetLive() {
    std::lock_guard lock(m_mutex);
    Live l;
    if (!m_box) {
        return l;
    }
    l.running = true;
    l.job = m_running;
    l.phase = int(m_box->GetPhase());
    l.net = m_box->NetBytes();
    l.source_size = m_source_size;
    l.ready = m_box->ReadyBytes();
    l.written = m_box->WrittenBytes();
    l.total = m_box->TotalBytes();
    l.checked = m_box->NcasChecked();
    l.ncas = m_box->NcasTotal();
    l.threads = m_box->Threads();
    l.connections = m_connections;
    const double now = Now();
    l.elapsed = now - m_started;
    // rates over half a second, smoothed
    if (m_sample_t == 0) {
        m_sample_t = now;
        m_sample_net = l.net;
        m_sample_ready = l.ready;
        m_sample_written = l.written;
    } else if (now - m_sample_t >= 0.5) {
        const double dt = now - m_sample_t;
        const auto rate = [dt](u64 a, u64 b) { return double(a - b) / dt / (1024.0 * 1024.0); };
        const auto ease = [](double& v, double x) { v = v == 0 ? x : v * 0.6 + x * 0.4; };
        ease(m_rate_net, rate(l.net, m_sample_net));
        ease(m_rate_ready, rate(l.ready, m_sample_ready));
        ease(m_rate_written, rate(l.written, m_sample_written));
        m_sample_t = now;
        m_sample_net = l.net;
        m_sample_ready = l.ready;
        m_sample_written = l.written;
    }
    l.net_rate = m_rate_net;
    l.ready_rate = m_rate_ready;
    l.write_rate = m_rate_written;
    return l;
}

void InstallQueue::Run() {
    std::unique_lock lock(m_mutex);
    while (!m_quit) {
        auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [](const InstallJob& j) { return j.state == InstallJob::State::Waiting; });
        if (it == m_jobs.end()) {
            m_cv.wait(lock);
            continue;
        }
        it->state = InstallJob::State::Running;
        InstallJob job = *it;
        lock.unlock();
        RunJob(job);
        lock.lock();
        for (auto& j : m_jobs) {
            if (j.id == job.id) {
                j.state = job.state;
                j.error = job.error;
                j.seconds = job.seconds;
                j.written = job.written;
            }
        }
        m_version++;
    }
}

void InstallQueue::RunJob(InstallJob& job) {
    ProgressBox box;
    int connections;
    bool test;
    {
        std::lock_guard lock(m_mutex);
        m_box = &box;
        m_running = job.id;
        m_started = Now();
        m_source_size = 0;
        m_sample_t = 0;
        m_rate_net = m_rate_ready = m_rate_written = 0;
        connections = m_connections;
        test = m_download_test;
        if (m_quit) {
            box.RequestCancel();
        }
    }

    // the CPU at its fast clock and the console awake until the job ends
    net::Boost boost;
    nxdock_log_open("sdmc:/switch/NXDock/install.log");
    log_write("[NXDock] %s: %s (%s) %s\n", NXDOCK_VERSION, job.game.c_str(), job.label.c_str(), job.format.c_str());

    const bool applet = net::IsAppletMode();
    HttpSource::Options options;
    options.connections = ConnectionsFor(connections, applet);
    options.piece = applet ? 1024 * 1024 : 8 * 1024 * 1024;
    options.ahead = 2;
    Result rc;
    {
        HttpSource source(job.url, job.key, options, &box);
        rc = source.Open();
        {
            std::lock_guard lock(m_mutex);
            m_source_size = u64(std::max<s64>(0, source.Size()));
        }
        if (R_SUCCEEDED(rc)) {
            if (test) {
                rc = DownloadCheck(source, box);
            } else {
                sphaira::yati::container::Nsp nsp(&source);
                sphaira::yati::ConfigOverride config;
                config.sd_card_install = true; // the SD card only, in this step
                rc = sphaira::yati::InstallFromContainer(&box, &nsp, config);
            }
        }
        source.SignalCancel();
    }
    log_write("[NXDock] result 0x%X\n", rc);
    nxdock_log_close();

    job.seconds = Now() - m_started;
    job.written = box.WrittenBytes();
    if (R_SUCCEEDED(rc)) {
        job.state = InstallJob::State::Done;
    } else {
        job.state = R_DESCRIPTION(rc) == u32(NXDockResult::TransferCancelled) && R_MODULE(rc) == Module_NXDock ? InstallJob::State::Cancelled
                                                                                                                 : InstallJob::State::Failed;
        s64 free_bytes = -1;
        FsFileSystem fs;
        if (R_SUCCEEDED(fsOpenSdCardFileSystem(&fs))) {
            char root[FS_MAX_PATH] = "/";
            fsFsGetFreeSpace(&fs, root, &free_bytes);
            fsFsClose(&fs);
        }
        job.error = InstallErrorText(rc, box.NeededBytes(), box.NeededFirmware(), free_bytes);
    }
    auto installed = ReadInstalled();

    std::lock_guard lock(m_mutex);
    m_box = nullptr;
    m_running = 0;
    m_installed = std::move(installed);
}

} // namespace nxdock
