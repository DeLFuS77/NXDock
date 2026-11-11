// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "install_queue.hpp"

#include "compress_nx.hpp"

#include "errors.hpp"
#include "history.hpp"
#include "file_source.hpp"
#include "http_source.hpp"
#include "mtp.hpp"
#include "usb_tinfoil.hpp"
#include "i18n.hpp"
#include "uninstall.hpp"
#include "net.hpp"
#include "ui/progress_box.hpp"
#include "yati/container/nsp.hpp"
#include "yati/container/xci.hpp"
#include "yati/yati.hpp"
#include "resume.hpp"
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

// The download-only check, for an emulator that cannot install: every NCA of the NSP is read
// through the same source and its SHA-256 compared with the content id in its name. Nothing is
// written. Turned on with download_test=1 in config.ini.
Result DownloadCheck(sphaira::yati::source::Base& source, sphaira::yati::container::Base& container, ProgressBox& box) {
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
Result OpenContainer(sphaira::yati::source::Base& source, std::unique_ptr<sphaira::yati::container::Base>& out, const char*& kind) {
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

std::string InstallErrorText(unsigned rc, unsigned long long needed, unsigned firmware, long long free_bytes, bool nand, bool install) {
    ErrorContext ctx;
    ctx.needed = needed;
    ctx.firmware = firmware;
    const u32 hos = hosversionGet();
    ctx.console_firmware = (HOSVER_MAJOR(hos) << 26) | (HOSVER_MINOR(hos) << 20) | (HOSVER_MICRO(hos) << 16);
    ctx.free_bytes = free_bytes;
    ctx.nand = nand;
    ctx.install = install;
    return PackError(ExplainError(rc, ctx));
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
            if (m_merging) {
                // installed already: only the merge stops, the update stays installed
                m_merge_stop = true;
            } else if (m_box) {
                m_box->RequestCancel();
            }
        } else {
            m_jobs.erase(it);
        }
        return;
    }
}

namespace {

ResumeJob ToResume(const InstallJob& j) {
    ResumeJob r;
    auto& f = r.fields;
    f["game"] = j.game;
    f["label"] = j.label;
    f["cover"] = j.cover;
    f["color"] = j.color_key;
    f["file"] = j.file_id;
    f["url"] = j.url;
    f["format"] = j.format;
    f["origin"] = std::to_string(int(j.origin));
    f["kind"] = std::to_string(int(j.kind));
    f["title"] = std::to_string(j.title_id);
    f["size"] = std::to_string(j.size);
    f["nand"] = j.nand ? "1" : "0";
    f["system"] = std::to_string(j.required_system);
    f["version"] = std::to_string(j.version);
    f["downgrade"] = j.downgrade ? "1" : "0";
    f["firmware_ok"] = j.firmware_ok ? "1" : "0";
    r.parts = j.parts;
    return r;
}

InstallJob FromResume(const ResumeJob& r, const std::string& key) {
    const auto get = [&r](const char* k) {
        auto it = r.fields.find(k);
        return it == r.fields.end() ? std::string() : it->second;
    };
    InstallJob j;
    j.game = get("game");
    j.label = get("label");
    j.cover = get("cover");
    j.color_key = get("color");
    j.file_id = get("file");
    j.url = get("url");
    j.format = get("format");
    j.origin = InstallJob::Origin(std::atoi(get("origin").c_str()));
    j.kind = CatalogFile::Kind(std::atoi(get("kind").c_str()));
    j.title_id = std::strtoull(get("title").c_str(), nullptr, 10);
    j.size = std::strtoull(get("size").c_str(), nullptr, 10);
    j.nand = get("nand") == "1";
    j.required_system = u32(std::strtoul(get("system").c_str(), nullptr, 10));
    j.version = u32(std::strtoul(get("version").c_str(), nullptr, 10));
    j.downgrade = get("downgrade") == "1";
    j.firmware_ok = get("firmware_ok") == "1";
    j.checked = true; // asked about before the console went off
    j.parts = r.parts;
    j.resumable = true;
    if (j.origin == InstallJob::Origin::Library) j.key = key;
    return j;
}

} // namespace

std::vector<InstallJob> ResumeJobs(const ResumeState& state, const std::string& key) {
    std::vector<InstallJob> jobs;
    jobs.push_back(FromResume(state.job, key));
    jobs.back().resumed = true;
    for (const auto& w : state.waiting) jobs.push_back(FromResume(w, key));
    return jobs;
}

namespace {

// The placeholders of these parts deleted, where they are.
void DeletePlaceholders(const std::vector<ResumePart>& parts) {
    if (parts.empty()) return;
    if (R_SUCCEEDED(ncmInitialize())) {
        for (const bool sd : {true, false}) {
            NcmContentStorage cs{};
            if (R_FAILED(ncmOpenContentStorage(&cs, sd ? NcmStorageId_SdCard : NcmStorageId_BuiltInUser))) continue;
            for (const auto& p : parts) {
                NcmPlaceHolderId ph{};
                if (p.sd != sd || !ResumeUnhex(p.placeholder, &ph, sizeof(ph))) continue;
                bool has = false;
                if (R_SUCCEEDED(ncmContentStorageHasPlaceHolder(&cs, &has, &ph)) && has) ncmContentStorageDeletePlaceHolder(&cs, &ph);
            }
            ncmContentStorageClose(&cs);
        }
        ncmExit();
    }
}

} // namespace

std::set<std::string> ResumePlaceholders(bool sd) {
    std::set<std::string> out;
    ResumeState state;
    if (!LoadResume(kResumeDir, state)) return out;
    for (const auto& p : state.parts) {
        if (p.sd == sd) out.insert(p.placeholder);
    }
    return out;
}

void DiscardResume(const ResumeState& state) {
    DeletePlaceholders(state.parts);
    ClearResume(kResumeDir);
}

void InstallQueue::SetPaused(bool paused) {
    {
        std::lock_guard lock(m_mutex);
        m_paused = paused;
        m_version++;
    }
    m_cv.notify_all();
}

bool InstallQueue::Paused() {
    std::lock_guard lock(m_mutex);
    return m_paused;
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

void InstallQueue::SetModsCheck(ModsCheck fn) {
    std::lock_guard lock(m_mutex);
    m_mods_check = std::move(fn);
}

// A question for the user, from the queue's thread: true to go on.
bool InstallQueue::AskAbout(const Ask& ask) {
    std::unique_lock lock(m_mutex);
    m_ask = ask;
    m_ask.waiting = true;
    m_answered = false;
    m_version++;
    m_cv.wait(lock, [this] { return m_answered || m_quit; });
    m_ask = Ask{};
    return m_answered && m_go_on;
}

// Before an update: cheats and mods that would stop working are said first (19A).
bool InstallQueue::ModsBefore(u64 app, u32 to, const std::string& game) {
    ModsCheck fn;
    u32 from = 0;
    {
        std::lock_guard lock(m_mutex);
        fn = m_mods_check;
        auto it = m_installed.find(app);
        if (it == m_installed.end() || !it->second.base) return true; // not installed: nothing to break
        from = it->second.patch;
    }
    if (!fn) return true;
    const std::string what = fn(app, from, to);
    if (what.empty()) return true;
    Ask ask;
    ask.game = game;
    ask.error = what;
    ask.why = 10;
    ask.app = app;
    return AskAbout(ask);
}

void InstallQueue::SetSaveCopy(SaveCopy fn) {
    std::lock_guard lock(m_mutex);
    m_save_copy = std::move(fn);
}

InstallQueue::Ask InstallQueue::PendingAsk() {
    std::lock_guard lock(m_mutex);
    return m_ask;
}

void InstallQueue::Answer(bool go_on) {
    {
        std::lock_guard lock(m_mutex);
        if (!m_ask.waiting) return;
        m_answered = true;
        m_go_on = go_on;
    }
    m_cv.notify_all();
}

// Before a change to a game: the saves of its users to the library (18A). False when the copy
// failed and the user chose not to go on.
bool InstallQueue::CopyBefore(u64 app, int why, u32 to, const std::string& game) {
    SaveCopy fn;
    u32 current = 0;
    {
        std::lock_guard lock(m_mutex);
        fn = m_save_copy;
        auto it = m_installed.find(app);
        if (it != m_installed.end()) current = it->second.patch;
    }
    if (!fn) return true;
    m_copy_done = 0;
    m_copy_total = 0;
    m_copying = true;
    const std::string error = fn(app, why, to, current, [this](u64 d, u64 t) {
        m_copy_done = d;
        m_copy_total = t;
    });
    m_copying = false;
    if (error.empty()) return true;
    log_write("[NXDock] the saves of %016lX could not be copied\n", app);
    Ask ask;
    ask.game = game;
    ask.error = error;
    ask.why = why;
    ask.app = app;
    return AskAbout(ask);
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
    l.saved = m_box->SavedBytes();
    l.copying_save = m_copying;
    l.copy_done = m_copy_done;
    l.copy_total = m_copy_total;
    l.merging = m_merging;
    l.merge_phase = m_merge_phase;
    l.merge_done = m_merge_done;
    l.merge_total = m_merge_total;
    l.merge_rate = m_merge_rate;
    l.installed_bytes = m_installed_bytes;
    l.installed_seconds = m_installed_seconds;
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
        if (it == m_jobs.end() || m_paused) {
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
                j.saved = job.saved;
                j.merged = job.merged;
                j.merge_saved = job.merge_saved;
                j.merge_error = job.merge_error;
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
    // the saves first, before an update or a repair (18A); a failed copy asks the user
    bool go_on = true;
    if (job.kind == CatalogFile::Kind::Update && job.repair.empty()) {
        go_on = ModsBefore(job.title_id & ~0x1FFFull, job.version, job.game);
    }
    if (go_on && (job.kind == CatalogFile::Kind::Update || !job.repair.empty())) {
        go_on = CopyBefore(job.title_id & ~0x1FFFull, job.downgrade ? 3 : job.repair.empty() ? 0 : 1, job.version, job.game);
    }
    Result rc = go_on ? Result(0) : Result(Result_TransferCancelled);
    // the journal, to go on if the console goes off (23A): files from the library or a disk
    ResumeState journal;
    std::mutex journal_mutex;
    const bool journaled = go_on && job.resumable && (job.origin == InstallJob::Origin::Library || job.origin == InstallJob::Origin::File);
    if (journaled) {
        if (job.resumed && LoadResume(kResumeDir, journal)) {
            // parts cut half way are written again: their placeholders go now
            DeletePlaceholders(ResumeTakeCut(journal));
        } else {
            journal = ResumeState{};
        }
        journal.job = ToResume(job);
        journal.waiting.clear();
        {
            std::lock_guard lock(m_mutex);
            for (const auto& w : m_jobs) {
                if (w.state == InstallJob::State::Waiting && w.resumable) journal.waiting.push_back(ToResume(w));
            }
        }
        SaveResume(kResumeDir, journal);
    }
    if (go_on) {
        // the source of the file, from where it comes
        std::unique_ptr<sphaira::yati::source::Base> holder;
        s64 size = 0;
        switch (job.origin) {
            case InstallJob::Origin::Library: {
                auto http = std::make_unique<HttpSource>(job.url, job.key, options, &box);
                rc = http->Open();
                size = http->Size();
                holder = std::move(http);
                break;
            }
            case InstallJob::Origin::File: {
                auto file = std::make_unique<FileSource>(job.parts);
                rc = file->GetOpenResult();
                size = file->Size();
                holder = std::move(file);
                break;
            }
            case InstallJob::Origin::Usb:
                holder = std::make_unique<TinfoilSource>(UsbLink(), job.parts.empty() ? std::string{} : job.parts.front());
                size = s64(job.size);
                break;
            case InstallJob::Origin::Mtp:
                holder = MtpStreamSource(&box);
                size = s64(job.size);
                if (!holder) rc = Result_TransferCancelled;
                break;
        }
        {
            std::lock_guard lock(m_mutex);
            m_source_size = u64(std::max<s64>(0, size));
        }
        if (R_SUCCEEDED(rc) && holder) {
            auto& source = *holder;
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
                // the check before installing warned and the user went on anyway (2A)
                config.allow_newer_firmware = job.firmware_ok;
                // going back to an older version, asked for on the page of the game (20A)
                if (job.downgrade) config.allow_downgrade = true;
                if (journaled) {
                    const bool sd = !job.nand;
                    config.resume.part = [&journal, &journal_mutex, sd](const NcmContentId& cid, const NcmPlaceHolderId& ph, s64 size, bool checked,
                                                                        const void* header, size_t header_size) {
                        std::lock_guard lock(journal_mutex);
                        const std::string id = ResumeHex(&cid, sizeof(cid));
                        // the header first: a part is said checked only once its header is kept
                        const bool kept = checked && (!header || SaveResumeHeader(kResumeDir, id, header, header_size));
                        ResumeNote(journal, id, ResumeHex(&ph, sizeof(ph)), size, kept, sd);
                        SaveResume(kResumeDir, journal);
                    };
                    config.resume.before = [&journal, &journal_mutex, sd](const NcmContentId& cid, NcmPlaceHolderId& ph, void* header,
                                                                          size_t header_size) {
                        std::lock_guard lock(journal_mutex);
                        const std::string id = ResumeHex(&cid, sizeof(cid));
                        std::string placeholder;
                        if (!ResumeLookup(journal, id, sd, placeholder) || !ResumeUnhex(placeholder, &ph, sizeof(ph))) return false;
                        return !header || LoadResumeHeader(kResumeDir, id, header, header_size);
                    };
                }
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
        if (holder) {
            holder->SignalCancel();
        }
    }
    log_write("[NXDock] result 0x%X\n", rc);
    nxdock_log_close();
    if (journaled) {
        // the job ended, well or not: its placeholders that were not registered go with the journal
        std::lock_guard lock(journal_mutex);
        DiscardResume(journal);
    }

    job.seconds = Now() - m_started;
    job.written = box.WrittenBytes();
    job.saved = box.SavedBytes();
    if (R_SUCCEEDED(rc)) {
        job.state = InstallJob::State::Done;
    } else {
        job.state = R_DESCRIPTION(rc) == u32(NXDockResult::TransferCancelled) && R_MODULE(rc) == Module_NXDock ? InstallJob::State::Cancelled
                                                                                                                 : InstallJob::State::Failed;
        const s64 free_bytes = StorageFreeSpace(job.nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard);
        job.error = InstallErrorText(rc, box.NeededBytes(), box.NeededFirmware(), free_bytes, job.nand);
    }
    if (job.state == InstallJob::State::Done && !test) {
        MergeAfter(job, box.Registered());
    }
    if (job.origin == InstallJob::Origin::Mtp) {
        MtpJobEnded(job.state == InstallJob::State::Done, job.error);
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

// An update installed over a game NXDock merged (it keeps its recipe): merged with it now, in the
// same job, so the game never stays with an update it cannot read (25G). A failure leaves the update
// installed, the game as it was, and the reason on the job; Compress games offers it again.
void InstallQueue::MergeAfter(InstallJob& job, const std::vector<NcmContentMetaKey>& keys) {
    u64 app = 0;
    for (const auto& k : keys) {
        if (k.type == NcmContentMetaType_Patch && IsMergedGame(k.id & ~u64(0xFFF))) {
            app = k.id & ~u64(0xFFF);
        }
    }
    if (!app) {
        return;
    }
    {
        std::lock_guard lock(m_mutex);
        m_merging = true;
        m_merge_phase = 1;
        m_merge_done = m_merge_total = m_merge_sample = 0;
        m_merge_t = Now();
        m_merge_rate = 0;
        m_installed_bytes = job.written;
        m_installed_seconds = job.seconds;
        m_merge_stop = false;
    }
    log_write("[NXDock] the update of a merged game: merging %016llX\n", (unsigned long long)app);
    // what the merge does goes to its own log (the one of the install is closed by now)
    nxdock_log_open("sdmc:/switch/NXDock/compress.log");
    u64 saved = 0;
    const std::string error = MergeUpdate(app, m_merge_stop, [this](int phase, uint64_t done, uint64_t total) {
        std::lock_guard lock(m_mutex);
        if (phase != m_merge_phase) {
            m_merge_sample = 0;
            m_merge_t = Now();
            m_merge_rate = 0;
        }
        m_merge_phase = phase;
        m_merge_done = done;
        m_merge_total = total;
        const double now = Now();
        if (now - m_merge_t >= 0.5) {
            const double rate = double(done - std::min(done, m_merge_sample)) / (now - m_merge_t) / (1024.0 * 1024.0);
            m_merge_rate = m_merge_rate == 0 ? rate : m_merge_rate * 0.6 + rate * 0.4;
            m_merge_t = now;
            m_merge_sample = done;
        }
    }, saved);
    nxdock_log_close();
    std::lock_guard lock(m_mutex);
    m_merging = false;
    if (error.empty()) {
        job.merged = true;
        job.merge_saved = saved;
    } else {
        job.merge_error = error;
    }
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
        std::string game;
        {
            std::lock_guard lock(m_mutex);
            game = m_session[*it].game;
        }
        // the saves first (18A): a removal the user did not want to go on with leaves it as it was
        if (!keys.empty() && !CopyBefore(keys.front().id & ~0x1FFFull, 2, 0, game)) {
            std::lock_guard lock(m_mutex);
            m_session[*it].state = SessionItem::State::Installed;
            continue;
        }
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
            item.error = InstallErrorText(rc, 0, 0, -1, nand, false);
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
