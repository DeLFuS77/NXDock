// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// MTP with libhaze (GPL-2.0, from Atmosphère's haze, as Sphaira uses it). The file systems the
// computer sees are written here; the way a file dropped in an install folder becomes a stream
// for the install core follows Sphaira's haze_helper.cpp (GPL-3.0).
#include "mtp.hpp"

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

#ifdef NXDOCK_MTP
#include <haze.h>
#endif

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

#ifdef NXDOCK_MTP
namespace {

constexpr Result kPathNotFound = MAKERESULT(2, 1);
constexpr Result kDeviceBusy = MAKERESULT(420, 20);
constexpr Result kNotAllowed = MAKERESULT(2, 6);

// The path inside a storage: without the storage's own name in front.
std::string Inside(const char* name, const char* path) {
    std::string p = path ? path : "";
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    const size_t n = std::strlen(name);
    if (n && p.compare(0, n, name) == 0 && (p.size() == n || p[n] == '/')) p.erase(0, n);
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    return "/" + p;
}

// The SD card itself.
class SdProxy final : public haze::FileSystemProxyImpl {
public:
    SdProxy() { m_ok = R_SUCCEEDED(fsOpenSdCardFileSystem(&m_fs)); }
    ~SdProxy() {
        if (m_ok) {
            fsFsCommit(&m_fs);
            fsFsClose(&m_fs);
        }
    }
    const char* GetName() const override { return "sd"; }
    const char* GetDisplayName() const override { return m_display.c_str(); }
    void SetDisplay(std::string s) { m_display = std::move(s); }

    Result GetTotalSpace(const char* path, s64* out) override { return fsFsGetTotalSpace(&m_fs, Path(path), out); }
    Result GetFreeSpace(const char* path, s64* out) override { return fsFsGetFreeSpace(&m_fs, Path(path), out); }
    Result GetEntryType(const char* path, haze::FileAttrType* out) override {
        FsDirEntryType type;
        R_TRY(fsFsGetEntryType(&m_fs, Path(path), &type));
        *out = type == FsDirEntryType_Dir ? haze::FileAttrType_DIR : haze::FileAttrType_FILE;
        R_SUCCEED();
    }
    Result CreateFile(const char* path, s64 size) override { return fsFsCreateFile(&m_fs, Path(path), size, 0); }
    Result DeleteFile(const char* path) override { return fsFsDeleteFile(&m_fs, Path(path)); }
    Result RenameFile(const char* a, const char* b) override {
        const std::string from = Path(a);
        return fsFsRenameFile(&m_fs, from.c_str(), Path(b));
    }
    Result OpenFile(const char* path, haze::FileOpenMode mode, haze::File* out) override {
        auto f = new FsFile();
        const u32 flags = mode == haze::FileOpenMode_WRITE ? FsOpenMode_Write | FsOpenMode_Append : FsOpenMode_Read;
        const Result rc = fsFsOpenFile(&m_fs, Path(path), flags, f);
        if (R_FAILED(rc)) {
            delete f;
            return rc;
        }
        out->impl = f;
        R_SUCCEED();
    }
    Result GetFileSize(haze::File* f, s64* out) override { return fsFileGetSize(static_cast<FsFile*>(f->impl), out); }
    Result SetFileSize(haze::File* f, s64 size) override { return fsFileSetSize(static_cast<FsFile*>(f->impl), size); }
    Result ReadFile(haze::File* f, s64 off, void* buf, u64 size, u64* got) override {
        return fsFileRead(static_cast<FsFile*>(f->impl), off, buf, size, FsReadOption_None, got);
    }
    Result WriteFile(haze::File* f, s64 off, const void* buf, u64 size) override {
        return fsFileWrite(static_cast<FsFile*>(f->impl), off, buf, size, FsWriteOption_None);
    }
    void CloseFile(haze::File* f) override {
        auto file = static_cast<FsFile*>(f->impl);
        if (file) {
            fsFileClose(file);
            delete file;
        }
        f->impl = nullptr;
    }
    Result CreateDirectory(const char* path) override { return fsFsCreateDirectory(&m_fs, Path(path)); }
    Result DeleteDirectoryRecursively(const char* path) override { return fsFsDeleteDirectoryRecursively(&m_fs, Path(path)); }
    Result RenameDirectory(const char* a, const char* b) override {
        const std::string from = Path(a);
        return fsFsRenameDirectory(&m_fs, from.c_str(), Path(b));
    }
    Result OpenDirectory(const char* path, haze::Dir* out) override {
        auto d = new FsDir();
        const Result rc = fsFsOpenDirectory(&m_fs, Path(path), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize, d);
        if (R_FAILED(rc)) {
            delete d;
            return rc;
        }
        out->impl = d;
        R_SUCCEED();
    }
    Result ReadDirectory(haze::Dir* d, s64* total, size_t max, haze::DirEntry* buf) override {
        std::vector<FsDirectoryEntry> entries(max);
        R_TRY(fsDirRead(static_cast<FsDir*>(d->impl), total, max, entries.data()));
        for (s64 i = 0; i < *total; i++) {
            std::strncpy(buf[i].name, entries[i].name, sizeof(buf[i].name) - 1);
        }
        R_SUCCEED();
    }
    Result GetDirectoryEntryCount(haze::Dir* d, s64* out) override { return fsDirGetEntryCount(static_cast<FsDir*>(d->impl), out); }
    void CloseDirectory(haze::Dir* d) override {
        auto dir = static_cast<FsDir*>(d->impl);
        if (dir) {
            fsDirClose(dir);
            delete dir;
        }
        d->impl = nullptr;
    }

private:
    const char* Path(const char* p) {
        m_path = Inside(GetName(), p);
        return m_path.c_str();
    }
    FsFileSystem m_fs{};
    bool m_ok = false;
    std::string m_path, m_display = "SD card";
};

// An install folder: what is dropped there goes to the install queue as it arrives.
class InstallProxy final : public haze::FileSystemProxyImpl {
public:
    InstallProxy(const char* name, std::string display, bool nand) : m_name(name), m_display(std::move(display)), m_nand(nand) {}
    const char* GetName() const override { return m_name; }
    const char* GetDisplayName() const override { return m_display.c_str(); }

    Result GetTotalSpace(const char*, s64* out) override {
        return ncmSpace(true, out);
    }
    Result GetFreeSpace(const char*, s64* out) override {
        return ncmSpace(false, out);
    }
    Result GetEntryType(const char* path, haze::FileAttrType* out) override {
        const std::string p = Inside(m_name, path);
        if (p == "/") {
            *out = haze::FileAttrType_DIR;
            R_SUCCEED();
        }
        std::lock_guard lock(m_mutex);
        R_UNLESS(Find(p) != m_files.end(), kPathNotFound);
        *out = haze::FileAttrType_FILE;
        R_SUCCEED();
    }
    Result GetEntryAttributes(const char* path, haze::FileAttr* out) override {
        R_TRY(GetEntryType(path, &out->type));
        if (out->type == haze::FileAttrType_FILE) {
            std::lock_guard lock(m_mutex);
            out->size = u64(Find(Inside(m_name, path))->size);
        }
        R_SUCCEED();
    }
    Result CreateFile(const char* path, s64 size) override {
        const std::string p = Inside(m_name, path);
        R_UNLESS(!GameFormat(p).empty(), kNotAllowed);
        {
            std::lock_guard lock(g_mutex);
            if (m_nand && !g_view.nand_allowed) R_THROW(kNotAllowed);
            // one file at a time, and only when the queue is free: the computer waits and retries
            if (g_pipe || g_handed || (g_busy && g_busy())) R_THROW(kDeviceBusy);
        }
        std::lock_guard lock(m_mutex);
        if (Find(p) == m_files.end()) m_files.push_back({p, size});
        R_SUCCEED();
    }
    Result DeleteFile(const char* path) override {
        std::lock_guard lock(m_mutex);
        auto it = Find(Inside(m_name, path));
        if (it != m_files.end()) m_files.erase(it);
        R_SUCCEED();
    }
    Result RenameFile(const char*, const char*) override { R_THROW(kNotAllowed); }
    Result OpenFile(const char* path, haze::FileOpenMode mode, haze::File* out) override {
        const std::string p = Inside(m_name, path);
        s64 size = 0;
        {
            std::lock_guard lock(m_mutex);
            auto it = Find(p);
            R_UNLESS(it != m_files.end(), kPathNotFound);
            size = it->size;
        }
        if (mode != haze::FileOpenMode_WRITE) {
            out->impl = nullptr;
            R_SUCCEED();
        }
        auto pipe = std::make_shared<Pipe>();
        InstallJob job;
        const std::string name = p.substr(1);
        job.game = name;
        job.label = m_nand ? TrM().mtp_to_nand : TrM().mtp_to_sd;
        job.format = GameFormat(name);
        job.origin = InstallJob::Origin::Mtp;
        job.parts = {name};
        job.file_id = "mtp:" + name;
        job.size = u64(size);
        job.nand = m_nand;
        job.color_key = name;
        {
            std::lock_guard lock(g_mutex);
            R_UNLESS(!g_pipe && !g_handed, kDeviceBusy);
            g_pipe = pipe;
            g_handed = pipe;
            g_view.file = name;
            g_view.size = u64(size);
            g_view.received = 0;
        }
        if (!g_enqueue || !g_enqueue(std::move(job))) {
            std::lock_guard lock(g_mutex);
            g_pipe.reset();
            g_handed.reset();
            g_view.file.clear();
            R_THROW(kDeviceBusy);
        }
        out->impl = new std::shared_ptr<Pipe>(pipe);
        R_SUCCEED();
    }
    Result GetFileSize(haze::File*, s64* out) override {
        *out = 0;
        R_SUCCEED();
    }
    Result SetFileSize(haze::File*, s64) override { R_SUCCEED(); }
    Result ReadFile(haze::File*, s64, void*, u64, u64* got) override {
        *got = 0;
        R_SUCCEED();
    }
    Result WriteFile(haze::File* f, s64, const void* buf, u64 size) override {
        auto pipe = static_cast<std::shared_ptr<Pipe>*>(f->impl);
        R_UNLESS(pipe && (*pipe)->Push(buf, size_t(size)), kNotAllowed);
        std::lock_guard lock(g_mutex);
        g_view.received = (*pipe)->Received();
        R_SUCCEED();
    }
    void CloseFile(haze::File* f) override {
        auto pipe = static_cast<std::shared_ptr<Pipe>*>(f->impl);
        if (pipe) {
            (*pipe)->Close();
            std::lock_guard lock(g_mutex);
            if (g_pipe == *pipe) {
                g_pipe.reset();
                g_view.file.clear();
            }
            delete pipe;
        }
        f->impl = nullptr;
        // the file is not kept: it leaves the folder once it was handed over
        std::lock_guard lock(m_mutex);
        m_files.clear();
    }
    Result CreateDirectory(const char*) override { R_THROW(kNotAllowed); }
    Result DeleteDirectoryRecursively(const char*) override { R_THROW(kNotAllowed); }
    Result RenameDirectory(const char*, const char*) override { R_THROW(kNotAllowed); }
    Result OpenDirectory(const char*, haze::Dir* out) override {
        out->impl = nullptr;
        R_SUCCEED();
    }
    Result ReadDirectory(haze::Dir*, s64* total, size_t max, haze::DirEntry* buf) override {
        std::lock_guard lock(m_mutex);
        *total = 0;
        for (size_t i = 0; i < m_files.size() && i < max; i++) {
            std::strncpy(buf[i].name, m_files[i].path.c_str() + 1, sizeof(buf[i].name) - 1);
            (*total)++;
        }
        R_SUCCEED();
    }
    Result GetDirectoryEntryCount(haze::Dir*, s64* out) override {
        std::lock_guard lock(m_mutex);
        *out = s64(m_files.size());
        R_SUCCEED();
    }
    void CloseDirectory(haze::Dir*) override {}

private:
    struct Entry {
        std::string path;
        s64 size;
    };
    std::vector<Entry>::iterator Find(const std::string& p) {
        return std::find_if(m_files.begin(), m_files.end(), [&](const Entry& e) { return e.path == p; });
    }
    Result ncmSpace(bool total, s64* out) {
        NcmContentStorage cs{};
        R_TRY(ncmOpenContentStorage(&cs, m_nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard));
        const Result rc = total ? ncmContentStorageGetTotalSpaceSize(&cs, out) : ncmContentStorageGetFreeSpaceSize(&cs, out);
        ncmContentStorageClose(&cs);
        return rc;
    }
    const char* m_name;
    std::string m_display;
    bool m_nand;
    std::mutex m_mutex;
    std::vector<Entry> m_files;
};

bool g_running = false;
bool g_ncm = false;
haze::FsEntries g_entries;

void OnHaze(const haze::CallbackData*) {}

} // namespace

bool MtpStart(std::function<bool(InstallJob)> enqueue, std::function<bool()> queue_busy) {
    std::lock_guard lock(g_mutex);
    if (g_running) return true;
    g_enqueue = std::move(enqueue);
    g_busy = std::move(queue_busy);
    g_view = MtpView{};
    g_ncm = R_SUCCEEDED(ncmInitialize());
    const More& m = TrM();
    auto sd = std::make_shared<SdProxy>();
    sd->SetDisplay(m.mtp_sd_card);
    g_entries.clear();
    g_entries.push_back(std::make_shared<InstallProxy>("install_sd", m.mtp_install_sd, false));
    g_entries.push_back(std::make_shared<InstallProxy>("install_nand", m.mtp_install_nand, true));
    g_entries.push_back(sd);
    g_running = haze::Initialize(OnHaze, g_entries, 0x057e, 0x201d, "NXDock");
    g_view.running = g_running;
    return g_running;
}

void MtpStop() {
    {
        std::lock_guard lock(g_mutex);
        if (!g_running) return;
        // a file still arriving or waiting is broken, so the MTP thread can leave
        if (g_pipe) g_pipe->Break();
        if (g_handed) g_handed->Break();
    }
    haze::Exit();
    std::lock_guard lock(g_mutex);
    g_entries.clear();
    g_pipe.reset();
    g_handed.reset();
    g_running = false;
    g_view.running = false;
    if (g_ncm) {
        ncmExit();
        g_ncm = false;
    }
}

MtpView MtpSnapshot() {
    std::lock_guard lock(g_mutex);
    MtpView v = g_view;
    UsbState state = UsbState_Detached;
    v.connected = g_running && R_SUCCEEDED(usbDsGetState(&state)) && state == UsbState_Configured;
    return v;
}

#else

bool MtpStart(std::function<bool(InstallJob)>, std::function<bool()>) {
    return false;
}
void MtpStop() {}
MtpView MtpSnapshot() {
    return {};
}

#endif

} // namespace nxdock
