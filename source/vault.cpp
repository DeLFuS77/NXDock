// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "vault.hpp"

#include "defines.hpp"
#include "errors.hpp"
#include "http.hpp"
#include "i18n_more.hpp"
#include "zip.hpp"

#include <switch.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

namespace nxdock {
namespace {

constexpr const char* kTmp = "sdmc:/switch/NXDock/tmp";
constexpr unsigned long long kMaxSave = 1024ull * 1024 * 1024;

std::string Encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// A packed error of the vault: a title and what to do, with the code when there is one.
std::string VaultError(const char* title, const char* todo, unsigned rc = 0, const std::string& what = {}) {
    ErrorText e;
    e.title = title;
    e.what = what;
    e.todo = todo;
    if (rc) e.code = ErrorCode(rc);
    return PackError(e);
}

std::string HttpError(const http::Response& r) {
    const More& m = TrM();
    if (r.status == 0) return VaultError(m.e_network_t, m.e_network_d, 0, r.error);
    char what[64];
    std::snprintf(what, sizeof(what), "HTTP %ld", r.status);
    return VaultError(m.e_refused_t, m.e_refused_d, 0, what);
}

FsSaveDataAttribute Attribute(const unsigned char uid[16], unsigned long long app_id) {
    FsSaveDataAttribute attr{};
    attr.application_id = app_id;
    std::memcpy(&attr.uid, uid, sizeof(attr.uid));
    attr.save_data_type = FsSaveDataType_Account;
    return attr;
}

// Every file and folder of a save, with its path from the root ("/").
Result Walk(FsFileSystem* fs, const std::string& dir, std::vector<std::pair<std::string, s64>>& out) {
    FsDir d;
    char path[FS_MAX_PATH];
    std::snprintf(path, sizeof(path), "%s", dir.c_str());
    R_TRY(fsFsOpenDirectory(fs, path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d));
    std::vector<FsDirectoryEntry> entries(64);
    std::vector<std::string> subdirs;
    while (true) {
        s64 n = 0;
        if (R_FAILED(fsDirRead(&d, &n, entries.size(), entries.data())) || n == 0) break;
        for (s64 i = 0; i < n; i++) {
            const std::string p = (dir == "/" ? "/" : dir + "/") + entries[i].name;
            if (entries[i].type == FsDirEntryType_Dir) {
                out.push_back({p + "/", -1});
                subdirs.push_back(p);
            } else {
                out.push_back({p, entries[i].file_size});
            }
        }
    }
    fsDirClose(&d);
    for (const auto& s : subdirs) R_TRY(Walk(fs, s, out));
    R_SUCCEED();
}

// The save to a zip on the SD card.
Result SaveToZip(const unsigned char uid[16], unsigned long long app_id, const std::string& zip_path,
                 const std::function<void(unsigned long long, unsigned long long)>& progress) {
    FsFileSystem fs;
    const FsSaveDataAttribute attr = Attribute(uid, app_id);
    R_TRY(fsOpenSaveDataFileSystem(&fs, FsSaveDataSpaceId_User, &attr));
    ON_SCOPE_EXIT(fsFsClose(&fs));
    std::vector<std::pair<std::string, s64>> items;
    R_TRY(Walk(&fs, "/", items));
    unsigned long long total = 0, done = 0;
    for (const auto& i : items) total += i.second > 0 ? u64(i.second) : 0;
    R_UNLESS(total <= kMaxSave, Result_NotEnoughSpace);
    zip::Writer w;
    R_UNLESS(w.Open(zip_path), Result_NotEnoughSpace);
    for (const auto& [path, size] : items) {
        const std::string name = path.substr(1);
        if (size < 0) {
            R_UNLESS(w.AddFolder(name), Result_NotEnoughSpace);
            continue;
        }
        FsFile f;
        char p[FS_MAX_PATH];
        std::snprintf(p, sizeof(p), "%s", path.c_str());
        R_TRY(fsFsOpenFile(&fs, p, FsOpenMode_Read, &f));
        const bool ok = w.AddFile(name, u64(size), [&](uint64_t off, void* buf, size_t n) {
            u64 got = 0;
            return R_SUCCEEDED(fsFileRead(&f, s64(off), buf, n, FsReadOption_None, &got)) && got == n;
        });
        fsFileClose(&f);
        R_UNLESS(ok, Result_NotEnoughSpace);
        done += u64(size);
        progress(done, total);
    }
    R_UNLESS(w.Close(), Result_NotEnoughSpace);
    R_SUCCEED();
}

// A zip over the save: everything in it goes, the zip's files come, then it is committed.
Result ZipToSave(const unsigned char uid[16], unsigned long long app_id, const std::string& zip_path,
                 const std::function<void(unsigned long long, unsigned long long)>& progress) {
    zip::Reader r;
    R_UNLESS(r.Open(zip_path), Result_UnknownContainer);
    unsigned long long total = 0, done = 0;
    for (const auto& e : r.Entries()) {
        R_UNLESS(zip::SafeName(e.name), Result_UnknownContainer);
        total += e.size;
    }
    FsFileSystem fs;
    const FsSaveDataAttribute attr = Attribute(uid, app_id);
    R_TRY(fsOpenSaveDataFileSystem(&fs, FsSaveDataSpaceId_User, &attr));
    ON_SCOPE_EXIT(fsFsClose(&fs));
    char root[FS_MAX_PATH] = "/";
    R_TRY(fsFsCleanDirectoryRecursively(&fs, root));
    for (const auto& e : r.Entries()) {
        char p[FS_MAX_PATH];
        std::snprintf(p, sizeof(p), "/%s", e.name.c_str());
        // every folder on the way
        for (char* s = std::strchr(p + 1, '/'); s; s = std::strchr(s + 1, '/')) {
            *s = 0;
            fsFsCreateDirectory(&fs, p);
            *s = '/';
        }
        if (e.Folder()) continue;
        fsFsDeleteFile(&fs, p);
        R_TRY(fsFsCreateFile(&fs, p, s64(e.size), 0));
        FsFile f;
        R_TRY(fsFsOpenFile(&fs, p, FsOpenMode_Write, &f));
        s64 off = 0;
        const bool ok = r.Extract(e, [&](const void* data, size_t n) {
            const bool w = R_SUCCEEDED(fsFileWrite(&f, off, data, n, FsWriteOption_None));
            off += s64(n);
            return w;
        });
        fsFileClose(&f);
        R_UNLESS(ok, Result_UnknownContainer);
        // a commit for each file keeps the journal of the save small
        R_TRY(fsFsCommit(&fs));
        done += e.size;
        progress(done, total);
    }
    return fsFsCommit(&fs);
}

// Sends a zip of the SD card to the vault of the library.
http::Response Upload(const std::string& url, const std::string& key, const std::string& zip_path,
                      const std::function<void(unsigned long long, unsigned long long)>& progress) {
    FILE* f = std::fopen(zip_path.c_str(), "rb");
    if (!f) return {0, {}, "zip"};
    std::fseek(f, 0, SEEK_END);
    const unsigned long long size = (unsigned long long)ftello(f);
    std::fseek(f, 0, SEEK_SET);
    const auto r = http::Send("POST", url, key, size, [f](char* buf, size_t max) { return long(std::fread(buf, 1, max, f)); },
                              [&](uint64_t done) {
                                  progress(done, size);
                                  return true;
                              });
    std::fclose(f);
    return r;
}

} // namespace

Vault::Vault() {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/NXDock", 0777);
    mkdir(kTmp, 0777);
}

Vault::~Vault() {
    if (m_thread.joinable()) m_thread.join();
}

std::vector<Vault::User> Vault::Users() {
    std::vector<User> out;
    if (R_FAILED(accountInitialize(AccountServiceType_Application))) return out;
    AccountUid uids[ACC_USER_LIST_SIZE];
    s32 n = 0;
    if (R_SUCCEEDED(accountListAllUsers(uids, ACC_USER_LIST_SIZE, &n))) {
        for (s32 i = 0; i < n; i++) {
            User u;
            std::memcpy(u.uid, &uids[i], sizeof(u.uid));
            AccountProfile profile;
            AccountProfileBase base{};
            if (R_SUCCEEDED(accountGetProfile(&profile, uids[i]))) {
                if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base))) u.name = base.nickname;
                accountProfileClose(&profile);
            }
            out.push_back(u);
        }
    }
    accountExit();
    return out;
}

std::vector<Vault::Save> Vault::Saves(const User& user) {
    std::vector<Save> out;
    FsSaveDataInfoReader reader;
    if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) return out;
    FsSaveDataInfo info[32];
    while (true) {
        s64 n = 0;
        if (R_FAILED(fsSaveDataInfoReaderRead(&reader, info, 32, &n)) || n == 0) break;
        for (s64 i = 0; i < n; i++) {
            if (info[i].save_data_type != FsSaveDataType_Account || std::memcmp(&info[i].uid, user.uid, 16) != 0) continue;
            out.push_back({info[i].application_id, info[i].size, {}});
        }
    }
    fsSaveDataInfoReaderClose(&reader);
    // the names, from the console
    const bool ns = R_SUCCEEDED(nsInitialize());
    if (ns) {
        auto control = std::make_unique<NsApplicationControlData>();
        for (auto& s : out) {
            u64 size = 0;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, s.app_id, control.get(), sizeof(*control), &size))) {
                NacpLanguageEntry* lang = nullptr;
                if (R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &lang)) && lang) s.name = lang->name;
            }
        }
        nsExit();
    }
    for (auto& s : out) {
        if (s.name.empty()) s.name = TitleHex(s.app_id);
    }
    return out;
}

bool Vault::Busy() {
    std::lock_guard lock(m_mutex);
    return m_busy;
}

Vault::View Vault::Snapshot() {
    std::lock_guard lock(m_mutex);
    return m_view;
}

void Vault::Run(std::function<void()> work) {
    if (m_thread.joinable()) m_thread.join();
    m_busy = true;
    m_view.error.clear();
    m_thread = std::thread(std::move(work));
}

void Vault::Done(const std::string& error, bool restore) {
    std::lock_guard lock(m_mutex);
    m_view.stage = Stage::Idle;
    m_view.error = error;
    m_view.last_restore = restore;
    m_view.finished++;
    m_busy = false;
}

void Vault::Progress(unsigned long long done, unsigned long long total, Stage stage) {
    std::lock_guard lock(m_mutex);
    m_view.stage = stage;
    m_view.done = done;
    m_view.total = total;
}

bool Vault::StartList(const std::string& url, const std::string& key) {
    if (Busy()) return false;
    Run([this, url, key] {
        const auto r = http::GetLarge(url + "/install/saves", key, 8000, 4 * 1024 * 1024);
        std::lock_guard lock(m_mutex);
        if (r.status == 200) {
            m_view.library = ParseSaves(r.body);
            m_view.listed = true;
            m_view.list_version++;
        }
        m_busy = false;
    });
    return true;
}

bool Vault::StartBackup(const std::string& url, const std::string& key, const User& user, unsigned long long app_id, unsigned version,
                        const std::string& note) {
    if (Busy()) return false;
    Run([this, url, key, user, app_id, version, note] {
        const More& m = TrM();
        const std::string zip_path = std::string(kTmp) + "/save.zip";
        Progress(0, 0, Stage::Copying);
        Result rc = SaveToZip(user.uid, app_id, zip_path, [this](u64 d, u64 t) { Progress(d, t, Stage::Copying); });
        if (R_FAILED(rc)) {
            std::remove(zip_path.c_str());
            Done(VaultError(m.v_copy_failed, m.v_copy_failed_d, rc));
            return;
        }
        const std::string target = url + "/install/saves/" + TitleHex(app_id) + "?note=" + Encode(note) +
                                   "&version=" + std::to_string(version) + "&account=" + Encode(user.name);
        const auto r = Upload(target, key, zip_path, [this](u64 d, u64 t) { Progress(d, t, Stage::Sending); });
        std::remove(zip_path.c_str());
        Done(r.status == 200 ? std::string{} : HttpError(r));
    });
    return true;
}

bool Vault::StartRestore(const std::string& url, const std::string& key, const User& user, unsigned long long app_id,
                         const LibrarySave& copy, const std::string& before_note) {
    if (Busy()) return false;
    Run([this, url, key, user, app_id, copy, before_note] {
        const More& m = TrM();
        const std::string in = std::string(kTmp) + "/restore.zip", before = std::string(kTmp) + "/before.zip";
        // 1. the copy of the library, to the SD card
        FILE* f = std::fopen(in.c_str(), "wb");
        if (!f) {
            Done(VaultError(m.e_storage_t, m.e_storage_d), true);
            return;
        }
        Progress(0, copy.size, Stage::Receiving);
        const auto r = http::Receive(url + "/install/saves/" + copy.title_id + "/" + copy.id, key, kMaxSave,
                                     [f](const char* d, size_t n) { return std::fwrite(d, 1, n, f) == n; },
                                     [this, &copy](uint64_t d) {
                                         Progress(d, copy.size, Stage::Receiving);
                                         return true;
                                     });
        const bool written = std::fclose(f) == 0;
        if (r.status != 200 || !written) {
            std::remove(in.c_str());
            Done(HttpError(r), true);
            return;
        }
        // 2. the save it replaces, to the library first
        Progress(0, 0, Stage::Copying);
        Result rc = SaveToZip(user.uid, app_id, before, [this](u64 d, u64 t) { Progress(d, t, Stage::Copying); });
        if (R_FAILED(rc)) {
            std::remove(in.c_str());
            std::remove(before.c_str());
            Done(VaultError(m.v_no_save, m.v_no_save_d, rc), true);
            return;
        }
        const std::string target = url + "/install/saves/" + TitleHex(app_id) + "?note=" + Encode(before_note) + "&account=" + Encode(user.name);
        const auto up = Upload(target, key, before, [this](u64 d, u64 t) { Progress(d, t, Stage::Sending); });
        if (up.status != 200) {
            // nothing was touched: the save stays as it was
            std::remove(in.c_str());
            std::remove(before.c_str());
            Done(HttpError(up), true);
            return;
        }
        // 3. the copy over the save; the one before stays on the SD card until the next restore
        rc = ZipToSave(user.uid, app_id, in, [this](u64 d, u64 t) { Progress(d, t, Stage::Restoring); });
        std::remove(in.c_str());
        Done(R_SUCCEEDED(rc) ? std::string{} : VaultError(m.v_restore_failed, m.v_restore_failed_d, rc), true);
    });
    return true;
}

} // namespace nxdock
