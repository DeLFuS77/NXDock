// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "sdcheck_nx.hpp"

#include "net.hpp"

#include <map>
#include <switch.h>
#include <cstdio>
#include <ctime>

namespace nxdock {
namespace {

constexpr const char* kFolder = "/switch/NXDock/sdcheck";

double Seconds() { return double(armTicksToNs(armGetSystemTick())) / 1e9; }

class NxIo final : public SdCheckIo {
public:
    ~NxIo() override { End(); }
    bool Begin() override {
        if (R_FAILED(fsOpenSdCardFileSystem(&m_fs))) return false;
        m_open = true;
        fsFsCreateDirectory(&m_fs, "/switch");
        fsFsCreateDirectory(&m_fs, "/switch/NXDock");
        fsFsDeleteDirectoryRecursively(&m_fs, kFolder);
        return R_SUCCEEDED(fsFsCreateDirectory(&m_fs, kFolder));
    }
    long long Free() override {
        s64 v = -1;
        return R_SUCCEEDED(fsFsGetFreeSpace(&m_fs, "/", &v)) ? v : -1;
    }
    long long Total() override {
        s64 v = -1;
        return R_SUCCEEDED(fsFsGetTotalSpace(&m_fs, "/", &v)) ? v : -1;
    }
    bool Create(int file, uint64_t size) override {
        const std::string path = Path(file);
        // the size taken at once, so every file covers its own stretch of the card
        if (R_FAILED(fsFsCreateFile(&m_fs, path.c_str(), s64(size), 0))) return false;
        FsFile f{};
        if (R_FAILED(fsFsOpenFile(&m_fs, path.c_str(), FsOpenMode_Read | FsOpenMode_Write, &f))) return false;
        m_files[file] = f;
        return true;
    }
    bool Write(int file, uint64_t off, const void* buf, size_t size) override {
        auto it = m_files.find(file);
        return it != m_files.end() && R_SUCCEEDED(fsFileWrite(&it->second, s64(off), buf, size, FsWriteOption_None));
    }
    bool Read(int file, uint64_t off, void* buf, size_t size) override {
        auto it = m_files.find(file);
        if (it == m_files.end()) {
            FsFile f{};
            if (R_FAILED(fsFsOpenFile(&m_fs, Path(file).c_str(), FsOpenMode_Read, &f))) return false;
            it = m_files.emplace(file, f).first;
        }
        u64 got = 0;
        return R_SUCCEEDED(fsFileRead(&it->second, s64(off), buf, size, FsReadOption_None, &got)) && got == size;
    }
    void Close(int file) override {
        auto it = m_files.find(file);
        if (it == m_files.end()) return;
        fsFileFlush(&it->second);
        fsFileClose(&it->second);
        m_files.erase(it);
    }
    void End() override {
        for (auto& [n, f] : m_files) fsFileClose(&f);
        m_files.clear();
        if (m_open) {
            fsFsDeleteDirectoryRecursively(&m_fs, kFolder);
            fsFsCommit(&m_fs);
            fsFsClose(&m_fs);
            m_open = false;
        }
    }
    double Now() override { return Seconds(); }

private:
    static std::string Path(int file) {
        char p[96];
        std::snprintf(p, sizeof(p), "%s/%04d.h2w", kFolder, file + 1);
        return p;
    }
    FsFileSystem m_fs{};
    bool m_open = false;
    std::map<int, FsFile> m_files;
};

} // namespace

SdChecker::~SdChecker() {
    m_progress.stop = true;
    if (m_thread.joinable()) m_thread.join();
}

void SdChecker::CleanLeftovers() {
    FsFileSystem fs{};
    if (R_FAILED(fsOpenSdCardFileSystem(&fs))) return;
    fsFsDeleteDirectoryRecursively(&fs, kFolder);
    fsFsClose(&fs);
}

void SdChecker::Space(long long& free_bytes, long long& total_bytes) {
    free_bytes = total_bytes = -1;
    FsFileSystem fs{};
    if (R_FAILED(fsOpenSdCardFileSystem(&fs))) return;
    s64 v = -1;
    if (R_SUCCEEDED(fsFsGetFreeSpace(&fs, "/", &v))) free_bytes = v;
    if (R_SUCCEEDED(fsFsGetTotalSpace(&fs, "/", &v))) total_bytes = v;
    fsFsClose(&fs);
}

bool SdChecker::Start(bool quick) {
    std::lock_guard lock(m_mutex);
    if (m_running) return false;
    if (m_thread.joinable()) m_thread.join();
    m_running = true;
    m_done = false;
    m_quick = quick;
    m_result = {};
    m_progress.stop = false;
    m_progress.phase = 0;
    m_progress.done = 0;
    m_progress.total = 0;
    m_started = Seconds();
    Space(m_free, m_total);
    m_thread = std::thread([this, quick] {
        net::Boost awake; // no sleep and no dimming while it runs
        NxIo io;
        const auto r = RunSdCheck(io, quick, (uint64_t(time(nullptr)) << 20) ^ armGetSystemTick(), m_progress);
        std::lock_guard lock(m_mutex);
        m_result = r;
        m_running = false;
        m_done = true;
    });
    return true;
}

void SdChecker::Stop() { m_progress.stop = true; }

SdChecker::View SdChecker::Snapshot() {
    std::lock_guard lock(m_mutex);
    View v;
    v.running = m_running;
    v.done = m_done;
    v.quick = m_quick;
    v.phase = m_progress.phase;
    v.bytes_done = m_progress.done;
    v.bytes = m_progress.total;
    v.write_mb = m_progress.write_mb;
    v.read_mb = m_progress.read_mb;
    v.elapsed = m_running ? Seconds() - m_started : m_result.seconds;
    v.free_bytes = m_free;
    v.total_bytes = m_total;
    v.result = m_result;
    return v;
}

} // namespace nxdock
