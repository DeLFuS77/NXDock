// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "install_queue.hpp"

#include "history.hpp"
#include "http_source.hpp"
#include "i18n.hpp"
#include "uninstall.hpp"
#include "net.hpp"
#include "ui/progress_box.hpp"
#include "yati/container/nsp.hpp"
#include "yati/container/xci.hpp"
#include "yati/yati.hpp"
#include "log.hpp"

#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

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
Result DownloadCheck(HttpSource& source, sphaira::yati::container::Base& container, ProgressBox& box) {
    sphaira::yati::container::Collections collections;
    R_TRY(container.GetCollections(collections));
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

// The container of the file, from its first bytes.
Result OpenContainer(HttpSource& source, std::unique_ptr<sphaira::yati::container::Base>& out, const char*& kind) {
    std::vector<unsigned char> head(kContainerHeadSize);
    u64 got = 0;
    R_TRY(source.Read(head.data(), 0, s64(head.size()), &got));
    switch (DetectContainer(head.data(), size_t(got))) {
        case ContainerKind::Nsp:
            out = std::make_unique<sphaira::yati::container::Nsp>(&source);
            kind = "PFS0";
            R_SUCCEED();
        case ContainerKind::Xci:
            out = std::make_unique<sphaira::yati::container::Xci>(&source);
            kind = "XCI";
            R_SUCCEED();
        default:
            R_THROW(Result_UnknownContainer);
    }
}

// Deletes the damaged parts of a content before it is repaired.
Result DeleteParts(NcmStorageId storage, const std::vector<NcmContentId>& ids) {
    R_TRY(ncmInitialize());
    ON_SCOPE_EXIT(ncmExit());
    NcmContentStorage cs{};
    R_TRY(ncmOpenContentStorage(&cs, storage));
    ON_SCOPE_EXIT(ncmContentStorageClose(&cs));
    for (const auto& id : ids) {
        bool has = false;
        if (R_SUCCEEDED(ncmContentStorageHas(&cs, &has, &id)) && has) {
            log_write("[NXDock] repair: deleting a damaged part\n");
            R_TRY(ncmContentStorageDelete(&cs, &id));
        }
    }
    R_SUCCEED();
}

HistoryEntry::Result HistoryResult(InstallJob::State state) {
    switch (state) {
        case InstallJob::State::Done: return HistoryEntry::Result::Installed;
        case InstallJob::State::Cancelled: return HistoryEntry::Result::Cancelled;
        default: return HistoryEntry::Result::Failed;
    }
}

} // namespace

bool IsCancelled(unsigned rc) {
    return R_MODULE(rc) == Module_NXDock && R_DESCRIPTION(rc) == u32(NXDockResult::TransferCancelled);
}

std::string InstallErrorText(unsigned rc, unsigned long long needed, unsigned firmware, long long free_bytes, bool nand) {
    const auto& s = Tr();
    char text[256];
    const u32 desc = R_DESCRIPTION(rc);
    if (R_MODULE(rc) == Module_NXDock) {
        switch (NXDockResult(desc)) {
            case NXDockResult::TransferCancelled: return s.err_cancelled;
            case NXDockResult::NotEnoughSpace:
                std::snprintf(text, sizeof(text), nand ? s.err_space_nand : s.err_space, Size(needed).c_str(), free_bytes >= 0 ? Size(u64(free_bytes)).c_str() : "?");
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
            case NXDockResult::YatiInvalidNczZstdError:
            case NXDockResult::NczBadTable:
            case NXDockResult::NczLz4Rebuild: return s.err_ncz;
            case NXDockResult::YatiTicketNotFound:
            case NXDockResult::YatiCertNotFound: return s.err_ticket;
            case NXDockResult::OutOfMemory: return s.err_memory;
            case NXDockResult::NczExperimental: return s.err_experimental;
            case NXDockResult::UnknownContainer:
            case NXDockResult::NspBadMagic:
            case NXDockResult::NspInvalidHeader:
            case NXDockResult::XciBadMagic:
            case NXDockResult::XciSecurePartitionNotFound: return s.err_container;
            case NXDockResult::GameRunning: return s.err_game_running;
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
        if (m_undo || m_undoing) {
            return false;
        }
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

std::vector<SessionItem> InstallQueue::Session() {
    std::lock_guard lock(m_mutex);
    return {m_session.rbegin(), m_session.rend()};
}

bool InstallQueue::StartUndo(std::vector<int> jobs) {
    {
        std::lock_guard lock(m_mutex);
        const bool busy = m_running != 0 || std::any_of(m_jobs.begin(), m_jobs.end(), [](const InstallJob& j) { return j.state == InstallJob::State::Waiting; });
        if (busy || m_undo || m_undoing) {
            return false;
        }
        m_undo = true;
        m_undo_jobs = std::move(jobs);
    }
    m_cv.notify_all();
    return true;
}

bool InstallQueue::StartVerify(u64 app_id) {
    {
        std::lock_guard lock(m_mutex);
        const bool busy = m_running != 0 || std::any_of(m_jobs.begin(), m_jobs.end(), [](const InstallJob& j) { return j.state == InstallJob::State::Waiting; });
        if (busy || m_undo || m_undoing || m_verify || m_verifying) {
            return false;
        }
        m_verify = true;
        m_verify_done = false;
        m_verify_app = app_id;
        m_verify_rc = 0;
        m_verify_result.clear();
        m_verify_progress.done = m_verify_progress.total = 0;
        m_verify_progress.parts_done = m_verify_progress.parts = 0;
        m_verify_progress.cancel = false;
    }
    m_cv.notify_all();
    return true;
}

InstallQueue::VerifyView InstallQueue::GetVerify() {
    std::lock_guard lock(m_mutex);
    VerifyView v;
    v.running = m_verify || m_verifying;
    v.done = m_verify_done;
    v.bytes_done = m_verify_progress.done;
    v.bytes = m_verify_progress.total;
    v.parts_done = m_verify_progress.parts_done;
    v.parts = m_verify_progress.parts;
    v.rc = m_verify_rc;
    if (m_verify_done) {
        v.result = m_verify_result;
    }
    return v;
}

void InstallQueue::CancelVerify() {
    m_verify_progress.cancel = true;
}

void InstallQueue::RunVerify() {
    u64 app;
    {
        std::lock_guard lock(m_mutex);
        app = m_verify_app;
    }
    std::vector<VerifiedContent> result;
    const Result rc = VerifyApplication(app, m_verify_progress, result);
    std::lock_guard lock(m_mutex);
    m_verify_rc = rc;
    m_verify_result = std::move(result);
}

bool InstallQueue::Undoing() {
    std::lock_guard lock(m_mutex);
    return m_undo || m_undoing;
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
        if (m_verify) {
            m_verify = false;
            m_verifying = true;
            lock.unlock();
            RunVerify();
            lock.lock();
            m_verifying = false;
            m_verify_done = true;
            continue;
        }
        if (m_undo) {
            m_undo = false;
            m_undoing = true;
            lock.unlock();
            RunUndo();
            lock.lock();
            m_undoing = false;
            m_version++;
            continue;
        }
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
            std::unique_ptr<sphaira::yati::container::Base> container;
            const char* kind = "";
            rc = OpenContainer(source, container, kind);
            log_write("[NXDock] container %s, to %s\n", kind, job.nand ? "the internal memory" : "the SD card");
            if (R_SUCCEEDED(rc) && test) {
                rc = DownloadCheck(source, *container, box);
            } else if (R_SUCCEEDED(rc)) {
                sphaira::yati::ConfigOverride config;
                // the internal memory only for a job the user confirmed on the page of the game
                config.sd_card_install = !job.nand;
                if (!job.repair.empty()) {
                    // a repair: the damaged parts go first; only what is missing then comes
                    config.repair = true;
                    rc = DeleteParts(job.nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard, job.repair);
                }
                if (R_SUCCEEDED(rc)) {
                    rc = sphaira::yati::InstallFromContainer(&box, container.get(), config);
                }
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
        const s64 free_bytes = StorageFreeSpace(job.nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard);
        job.error = InstallErrorText(rc, box.NeededBytes(), box.NeededFirmware(), free_bytes, job.nand);
    }
    auto installed = ReadInstalled();

    // the history on the SD card, also for a failed or cancelled job
    HistoryEntry entry;
    entry.when = std::time(nullptr);
    entry.game = job.game;
    entry.label = job.label;
    entry.format = job.format;
    entry.nand = job.nand;
    entry.size = m_source_size ? m_source_size : job.size;
    entry.written = job.written;
    entry.seconds = job.seconds;
    entry.result = HistoryResult(job.state);
    entry.error = job.error;
    if (!test) {
        History(HistoryPath()).Append(entry);
    }

    std::lock_guard lock(m_mutex);
    if (job.state == InstallJob::State::Done && !test) {
        SessionItem item;
        item.job = job.id;
        item.game = job.game;
        item.label = job.label;
        item.format = job.format;
        item.cover = job.cover;
        item.color_key = job.color_key;
        item.nand = job.nand;
        item.size = job.written;
        item.when = entry.when;
        item.keys = box.Registered();
        if (!item.keys.empty()) {
            m_session.push_back(std::move(item));
        }
    }
    m_box = nullptr;
    m_running = 0;
    m_installed = std::move(installed);
}

void InstallQueue::RunUndo() {
    std::vector<size_t> todo;
    {
        std::lock_guard lock(m_mutex);
        for (size_t i = 0; i < m_session.size(); i++) {
            auto& item = m_session[i];
            const bool wanted = m_undo_jobs.empty() || std::find(m_undo_jobs.begin(), m_undo_jobs.end(), item.job) != m_undo_jobs.end();
            if (wanted && (item.state == SessionItem::State::Installed || item.state == SessionItem::State::Failed)) {
                item.state = SessionItem::State::Removing;
                todo.push_back(i);
            }
        }
    }
    nxdock_log_open("sdmc:/switch/NXDock/install.log");
    // the latest first: an update goes before its game
    for (auto it = todo.rbegin(); it != todo.rend(); ++it) {
        std::vector<NcmContentMetaKey> keys;
        bool nand;
        {
            std::lock_guard lock(m_mutex);
            keys = m_session[*it].keys;
            nand = m_session[*it].nand;
        }
        Result rc = 0;
        for (const auto& key : keys) {
            const Result r = RemoveInstalled(key, nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard);
            if (R_FAILED(r) && R_SUCCEEDED(rc)) {
                rc = r;
            }
        }
        std::lock_guard lock(m_mutex);
        SessionItem& item = m_session[*it];
        log_write("[NXDock] removed %s (%s): 0x%X\n", item.game.c_str(), item.label.c_str(), rc);
        HistoryEntry entry;
        entry.when = std::time(nullptr);
        entry.game = item.game;
        entry.label = item.label;
        entry.format = item.format;
        entry.nand = item.nand;
        entry.size = item.size;
        entry.written = item.size;
        if (R_SUCCEEDED(rc)) {
            item.state = SessionItem::State::Removed;
            item.error.clear();
            entry.result = HistoryEntry::Result::Removed;
        } else {
            item.state = SessionItem::State::Failed;
            item.error = InstallErrorText(rc, 0, 0, -1);
            entry.result = HistoryEntry::Result::Failed;
            entry.error = item.error;
        }
        History(HistoryPath()).Append(entry);
    }
    nxdock_log_close();
    auto installed = ReadInstalled();
    std::lock_guard lock(m_mutex);
    m_installed = std::move(installed);
}

} // namespace nxdock
