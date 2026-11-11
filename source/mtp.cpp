// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// MTP with NXDock's own responder (mtp_proto.cpp, mtp_usb.cpp). The way a file dropped in an
// install folder becomes a stream for the install core follows Sphaira's haze_helper.cpp
// (GPL-3.0).
#include "mtp.hpp"
#include "mtp_more.hpp"

#include "defines.hpp"
#include "file_source.hpp"
#include "i18n_more.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include "mtp_storage.hpp"
#include "mtp_usb.hpp"

#include <switch.h>

#include <atomic>
#include <thread>

namespace nxdock {
namespace {

// The bytes of the file arriving, from the MTP thread to the install queue. Writes wait while
// the install is behind; a failed install breaks it and the computer is told the copy failed.
class Pipe {
public:
    static constexpr size_t kMax = 16 * 1024 * 1024;

    bool Push(const void* data, size_t size) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&] { return m_broken || m_queued < kMax; });
        if (m_broken) return false;
        const auto* p = static_cast<const unsigned char*>(data);
        m_chunks.emplace_back(p, p + size);
        m_queued += size;
        m_received += size;
        m_cv.notify_all();
        return true;
    }
    // 0 bytes at the end of the file, or when broken (a cancel)
    size_t Pop(void* out, size_t size) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&] { return m_broken || m_closed || !m_chunks.empty(); });
        if (m_broken || m_chunks.empty()) return 0;
        auto& front = m_chunks.front();
        const size_t n = std::min(size, front.size() - m_front_used);
        std::memcpy(out, front.data() + m_front_used, n);
        m_front_used += n;
        if (m_front_used == front.size()) {
            m_chunks.pop_front();
            m_front_used = 0;
        }
        m_queued -= n;
        m_cv.notify_all();
        return n;
    }
    void Close() {
        std::lock_guard lock(m_mutex);
        m_closed = true;
        m_cv.notify_all();
    }
    void Break() {
        std::lock_guard lock(m_mutex);
        m_broken = true;
        m_cv.notify_all();
    }
    bool Broken() {
        std::lock_guard lock(m_mutex);
        return m_broken;
    }
    u64 Received() {
        std::lock_guard lock(m_mutex);
        return m_received;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::vector<unsigned char>> m_chunks;
    size_t m_front_used = 0, m_queued = 0;
    u64 m_received = 0;
    bool m_closed = false, m_broken = false;
};

// A stream source over the pipe (the core reads it once, in order, and may skip forward).
class PipeSource final : public sphaira::yati::source::Base {
public:
    PipeSource(std::shared_ptr<Pipe> pipe, sphaira::ui::ProgressBox* box) : m_pipe(std::move(pipe)), m_box(box) {}
    bool IsStream() const override { return true; }
    void SignalCancel() override { m_pipe->Break(); }
    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override {
        R_UNLESS(off >= m_offset, Result_StreamBadSeek);
        auto out = static_cast<unsigned char*>(buf);
        *bytes_read = 0;
        std::vector<unsigned char> skip;
        while (size > 0) {
            if (m_box && m_box->Cancelled()) {
                m_pipe->Break();
                R_THROW(Result_TransferCancelled);
            }
            size_t got;
            if (off > m_offset) {
                skip.resize(size_t(std::min<s64>(off - m_offset, 1024 * 1024)));
                got = m_pipe->Pop(skip.data(), skip.size());
                R_UNLESS(got, Result_NetworkLost);
            } else {
                got = m_pipe->Pop(out, size_t(size));
                R_UNLESS(got, Result_NetworkLost);
                out += got;
                off += s64(got);
                size -= s64(got);
                *bytes_read += got;
            }
            m_offset += s64(got);
            if (m_box) m_box->AddNet(got);
        }
        R_SUCCEED();
    }

private:
    std::shared_ptr<Pipe> m_pipe;
    sphaira::ui::ProgressBox* m_box;
    s64 m_offset = 0;
};

std::mutex g_mutex;
std::function<bool(InstallJob)> g_enqueue;
std::function<bool()> g_busy;
std::shared_ptr<Pipe> g_pipe;     // the file arriving
std::shared_ptr<Pipe> g_handed;   // given to the queue, until the job takes it
MtpView g_view;

} // namespace

std::unique_ptr<sphaira::yati::source::Base> MtpStreamSource(sphaira::ui::ProgressBox* box) {
    std::lock_guard lock(g_mutex);
    if (!g_handed) return nullptr;
    auto source = std::make_unique<PipeSource>(g_handed, box);
    g_handed.reset();
    return source;
}

void MtpJobEnded(bool ok, const std::string& error) {
    std::lock_guard lock(g_mutex);
    if (ok) {
        g_view.done++;
    } else {
        g_view.failed++;
        g_view.last_error = error;
    }
    if (g_pipe) g_pipe->Break(); // anything left of it is not wanted any more
}

void MtpAllowNand(bool on) {
    std::lock_guard lock(g_mutex);
    g_view.nand_allowed = on;
}

namespace {

// An install folder: a game file dropped there goes to the install queue as it arrives.
class InstallStorage final : public mtp::Storage {
public:
    InstallStorage(std::string description, bool nand) : m_description(std::move(description)), m_nand(nand) {}
    std::string Description() const override { return m_description; }
    void Space(uint64_t& total, uint64_t& free) override {
        total = free = 0;
        NcmContentStorage cs{};
        if (R_SUCCEEDED(ncmOpenContentStorage(&cs, m_nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard))) {
            s64 t = 0, f = 0;
            ncmContentStorageGetTotalSpaceSize(&cs, &t);
            ncmContentStorageGetFreeSpaceSize(&cs, &f);
            total = uint64_t(t);
            free = uint64_t(f);
            ncmContentStorageClose(&cs);
        }
    }
    bool List(const std::string& dir, std::vector<mtp::Entry>& out) override {
        if (!dir.empty()) return false;
        std::lock_guard lock(g_mutex);
        // the file arriving now shows while it arrives
        if (g_pipe && !g_view.file.empty()) out.push_back({g_view.file, false, g_view.size, 0});
        return true;
    }
    bool Stat(const std::string& path, mtp::Entry& out) override {
        std::vector<mtp::Entry> list;
        List("", list);
        for (const auto& e : list) {
            if (e.name == path) {
                out = e;
                return true;
            }
        }
        return false;
    }
    std::unique_ptr<mtp::Source> Open(const std::string&) override { return nullptr; }
    std::unique_ptr<mtp::Sink> Create(const std::string& path, uint64_t size, bool& busy) override {
        busy = false;
        if (path.find('/') != std::string::npos || GameFormat(path).empty()) return nullptr;
        auto pipe = std::make_shared<Pipe>();
        {
            std::lock_guard lock(g_mutex);
            if (m_nand && !g_view.nand_allowed) return nullptr;
            // one file at a time, and only when the queue is free: the computer waits and retries
            if (g_pipe || g_handed || (g_busy && g_busy())) {
                busy = true;
                return nullptr;
            }
            g_pipe = pipe;
            g_handed = pipe;
            g_view.file = path;
            g_view.size = size;
            g_view.received = 0;
        }
        InstallJob job;
        job.game = path;
        job.label = m_nand ? TrM().mtp_to_nand : TrM().mtp_to_sd;
        job.format = GameFormat(path);
        job.origin = InstallJob::Origin::Mtp;
        job.parts = {path};
        job.file_id = "mtp:" + path;
        job.size = size;
        job.nand = m_nand;
        job.color_key = path;
        if (!g_enqueue || !g_enqueue(std::move(job))) {
            std::lock_guard lock(g_mutex);
            g_pipe.reset();
            g_handed.reset();
            g_view.file.clear();
            busy = true;
            return nullptr;
        }
        return std::make_unique<PipeSink>(pipe);
    }
    bool MakeDir(const std::string&) override { return false; }
    bool Remove(const std::string&, bool) override { return false; }
    bool Rename(const std::string&, const std::string&) override { return false; }

private:
    // the data of the file, into the pipe the install reads
    class PipeSink final : public mtp::Sink {
    public:
        explicit PipeSink(std::shared_ptr<Pipe> pipe) : m_pipe(std::move(pipe)) {}
        ~PipeSink() override {
            if (m_pipe) Close(false);
        }
        bool Write(const void* data, size_t size) override {
            if (!m_pipe->Push(data, size)) return false;
            std::lock_guard lock(g_mutex);
            g_view.received = m_pipe->Received();
            return true;
        }
        bool Close(bool ok) override {
            if (ok) m_pipe->Close();
            else m_pipe->Break();
            std::lock_guard lock(g_mutex);
            if (g_pipe == m_pipe) {
                g_pipe.reset();
                g_view.file.clear();
            }
            m_pipe.reset();
            return ok;
        }

    private:
        std::shared_ptr<Pipe> m_pipe;
    };

    std::string m_description;
    bool m_nand;
};

std::thread g_thread;
std::atomic<bool> g_running{false};
mtp::UsbTransport* g_usb = nullptr;
bool g_ncm = false;

} // namespace

bool MtpStart(std::function<bool(InstallJob)> enqueue, std::function<bool()> queue_busy, int units) {
    if (g_running) return true;
    {
        std::lock_guard lock(g_mutex);
        g_enqueue = std::move(enqueue);
        g_busy = std::move(queue_busy);
        g_view = MtpView{};
    }
    g_ncm = R_SUCCEEDED(ncmInitialize());
    static mtp::UsbTransport usb;
    if (!usb.Start()) {
        if (g_ncm) ncmExit();
        g_ncm = false;
        return false;
    }
    g_usb = &usb;
    const More& m = TrM();
    std::vector<std::shared_ptr<mtp::Storage>> storages = {
        std::make_shared<InstallStorage>(m.mtp_install_sd, false),
        std::make_shared<InstallStorage>(m.mtp_install_nand, true),
        std::make_shared<mtp::FolderStorage>("sdmc:/", m.mtp_sd_card),
    };
    MtpAddMore(storages, units); // the read-only folders of 8A
    g_running = true;
    {
        std::lock_guard lock(g_mutex);
        g_view.running = true;
    }
    g_thread = std::thread([storages] {
        // a new session each time a computer connects, until stopped
        while (g_running && g_usb->WaitConnected()) {
            mtp::Responder responder(*g_usb, storages, "NXDock");
            responder.Run();
            // the cable went: what was arriving is broken, the install leaves nothing
            std::lock_guard lock(g_mutex);
            if (g_pipe) g_pipe->Break();
        }
    });
    return true;
}

void MtpStop() {
    if (!g_running) return;
    g_running = false;
    {
        std::lock_guard lock(g_mutex);
        if (g_pipe) g_pipe->Break();
        if (g_handed) g_handed->Break();
    }
    g_usb->Stop();
    if (g_thread.joinable()) g_thread.join();
    MtpMoreStop();
    std::lock_guard lock(g_mutex);
    g_pipe.reset();
    g_handed.reset();
    g_view.running = false;
    if (g_ncm) {
        ncmExit();
        g_ncm = false;
    }
}

MtpView MtpSnapshot() {
    std::lock_guard lock(g_mutex);
    MtpView v = g_view;
    v.connected = g_running && g_usb && g_usb->Connected();
    return v;
}

} // namespace nxdock
