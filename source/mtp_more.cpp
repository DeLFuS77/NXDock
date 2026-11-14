// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// More folders for the computer over MTP (8A of the visual test of the new tools, 11/11): the games
// installed, each a folder with its game, update and DLC as NSP made as they are read (copying one
// to the computer dumps it, with its common ticket), and a folder "Mods & Cheats" that is
// atmosphere/contents/<title ID>/ (17); the saves, a zip for each game and user (and the console's
// own), and a zip dropped on a game restores it (18); the Album; and a USB disk plugged in. The
// Album and the USB disk are read only, and so is the rest of the games and the saves.
#include "mtp_more.hpp"

#include "i18n_more.hpp"
#include "i18n_tools.hpp"
#include "log.hpp"
#include "mods.hpp"
#include "mtp_storage.hpp"
#include "uninstall.hpp"
#include "usb_disks.hpp"
#include "vault.hpp"

#include <switch.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace nxdock {
namespace {

constexpr const char* kZips = "sdmc:/switch/NXDock/mtp-saves";
constexpr const char* kTmp = "sdmc:/switch/NXDock/tmp";
constexpr const char* kLog = "sdmc:/switch/NXDock/mtp.log";

std::mutex g_mutex;
std::function<std::vector<InstalledApp>()> g_installed;
bool g_album = false;

// A name a computer takes for a file or folder.
std::string Clean(const std::string& name) { return mtp::CleanName(name); }

// What is written from the computer, in mtp.log (kept small) and in the log of a job if one is
// open.
void Note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Note(const char* fmt, ...) {
    char text[600];
    va_list v;
    va_start(v, fmt);
    std::vsnprintf(text, sizeof(text), fmt, v);
    va_end(v);
    log_write("[NXDock] MTP: %s\n", text);
    struct stat st{};
    FILE* f = std::fopen(kLog, stat(kLog, &st) == 0 && st.st_size > 256 * 1024 ? "w" : "a");
    if (!f) return;
    char when[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(f, "%s %s\n", when, text);
    std::fclose(f);
}

// Whether a game is running: the one NXDock was opened from, or the one left open behind the Album.
bool GameRunning(unsigned long long app_id) {
    if (app_id == OwnProgramId()) return true;
    bool running = false;
    if (R_SUCCEEDED(pmdmntInitialize())) {
        u64 pid = 0;
        if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)) && R_SUCCEEDED(pminfoInitialize())) {
            u64 program = 0;
            running = R_SUCCEEDED(pminfoGetProgramId(&program, pid)) && program == app_id;
            pminfoExit();
        }
        pmdmntExit();
    }
    return running;
}

// The space of the SD card, where what the computer writes goes.
void SdSpace(uint64_t& total, uint64_t& free) {
    mtp::FolderStorage sd("sdmc:/", "");
    sd.Space(total, free);
}

std::string Hex16(unsigned long long id) {
    char text[20];
    std::snprintf(text, sizeof(text), "%016llX", id);
    return text;
}

// A folder seen read only.
class ReadOnlyFolder final : public mtp::Storage {
public:
    ReadOnlyFolder(std::string root, std::string description) : m_inner(std::move(root), std::move(description)) {}
    std::string Description() const override { return m_inner.Description(); }
    bool ReadOnly() const override { return true; }
    void Space(uint64_t& total, uint64_t& free) override { m_inner.Space(total, free); }
    bool List(const std::string& dir, std::vector<mtp::Entry>& out) override { return m_inner.List(dir, out); }
    bool Stat(const std::string& path, mtp::Entry& out) override { return m_inner.Stat(path, out); }
    std::unique_ptr<mtp::Source> Open(const std::string& path) override { return m_inner.Open(path); }
    std::unique_ptr<mtp::Sink> Create(const std::string&, uint64_t, bool& busy) override {
        busy = false;
        return nullptr;
    }
    bool MakeDir(const std::string&) override { return false; }
    bool Remove(const std::string&, bool) override { return false; }
    bool Rename(const std::string&, const std::string&) override { return false; }

private:
    mtp::FolderStorage m_inner;
};

class Refuse : public mtp::Storage {
public:
    bool ReadOnly() const override { return true; }
    std::unique_ptr<mtp::Sink> Create(const std::string&, uint64_t, bool& busy) override {
        busy = false;
        return nullptr;
    }
    bool MakeDir(const std::string&) override { return false; }
    bool Remove(const std::string&, bool) override { return false; }
    bool Rename(const std::string&, const std::string&) override { return false; }
};

class NspSource final : public mtp::Source {
public:
    explicit NspSource(std::shared_ptr<InstalledNsp> nsp) : m_nsp(std::move(nsp)) {}
    bool Read(uint64_t off, void* buf, size_t size) override { return m_nsp->read(off, buf, size); }

private:
    std::shared_ptr<InstalledNsp> m_nsp;
};

// The games installed: a folder each, an NSP for each of its contents, and the folder of its mods
// and cheats (17), the only place written to.
class InstalledStorage final : public Refuse {
public:
    explicit InstalledStorage(std::string description) : m_description(std::move(description)) {}
    std::string Description() const override { return m_description; }
    bool ReadOnly() const override { return false; }
    void Space(uint64_t& total, uint64_t& free) override {
        uint64_t sd_total = 0;
        SdSpace(sd_total, free); // the mods go to the SD card
        total = 0;
        NcmContentStorage cs{};
        if (R_SUCCEEDED(ncmOpenContentStorage(&cs, NcmStorageId_SdCard))) {
            s64 t = 0;
            ncmContentStorageGetTotalSpaceSize(&cs, &t);
            total = uint64_t(t);
            ncmContentStorageClose(&cs);
        }
        if (!total) total = sd_total;
    }
    bool List(const std::string& dir, std::vector<mtp::Entry>& out) override {
        Read();
        std::string inner;
        unsigned long long id = 0;
        switch (Mods(dir, inner, id)) {
            case mtp::ModsPath::Root:
                // nothing there yet: an empty folder
                if (!Contents(id).List("", out)) out.clear();
                return true;
            case mtp::ModsPath::Inside: return Contents(id).List(inner, out);
            case mtp::ModsPath::Bad: return false;
            case mtp::ModsPath::No: break;
        }
        std::lock_guard lock(m_mutex);
        if (dir.empty()) {
            for (const auto& [name, game] : m_games) out.push_back({name, true, 0, 0});
            return true;
        }
        const auto it = m_games.find(dir);
        if (it == m_games.end()) return false;
        out.push_back({mtp::kModsFolder, true, 0, 0});
        for (const auto& f : it->second) out.push_back({f.name, false, Size(dir + "/" + f.name, f), 0});
        return true;
    }
    bool Stat(const std::string& path, mtp::Entry& out) override {
        Read();
        std::string inner;
        unsigned long long id = 0;
        switch (Mods(path, inner, id)) {
            case mtp::ModsPath::Root: out = {mtp::kModsFolder, true, 0, 0}; return true;
            case mtp::ModsPath::Inside: return Contents(id).Stat(inner, out);
            case mtp::ModsPath::Bad: return false;
            case mtp::ModsPath::No: break;
        }
        std::lock_guard lock(m_mutex);
        const auto slash = path.find('/');
        if (slash == std::string::npos) {
            if (!m_games.count(path)) return false;
            out = {path, true, 0, 0};
            return true;
        }
        const auto it = m_games.find(path.substr(0, slash));
        if (it == m_games.end()) return false;
        for (const auto& f : it->second) {
            if (f.name == path.substr(slash + 1)) {
                out = {f.name, false, Size(path, f), 0};
                return true;
            }
        }
        return false;
    }
    std::unique_ptr<mtp::Source> Open(const std::string& path) override {
        std::string inner;
        unsigned long long id = 0;
        switch (Mods(path, inner, id)) {
            case mtp::ModsPath::Inside: return Contents(id).Open(inner);
            case mtp::ModsPath::Root:
            case mtp::ModsPath::Bad: return nullptr;
            case mtp::ModsPath::No: break;
        }
        mtp::Entry e;
        if (!Stat(path, e)) return nullptr;
        std::lock_guard lock(m_mutex);
        const auto it = m_nsps.find(path);
        if (it == m_nsps.end() || !it->second) return nullptr;
        return std::make_unique<NspSource>(it->second);
    }
    // only inside the folder of mods of a game, made the first time something is written there
    std::unique_ptr<mtp::Sink> Create(const std::string& path, uint64_t size, bool& busy) override {
        busy = false;
        std::string inner;
        unsigned long long id = 0;
        if (!Writable(path, inner, id)) return nullptr;
        auto sink = Contents(id).Create(inner, size, busy);
        Note("mods: %s %016llX/%s (%llu bytes)", sink ? "writing" : "could not write", id, inner.c_str(), (unsigned long long)size);
        return sink;
    }
    bool MakeDir(const std::string& path) override {
        std::string inner;
        unsigned long long id = 0;
        if (!Writable(path, inner, id)) return false;
        const bool ok = Contents(id).MakeDir(inner);
        Note("mods: %s %016llX/%s", ok ? "folder made" : "could not make the folder", id, inner.c_str());
        return ok;
    }
    bool Remove(const std::string& path, bool dir) override {
        std::string inner;
        unsigned long long id = 0;
        Read();
        if (Mods(path, inner, id) != mtp::ModsPath::Inside) return false;
        const bool ok = Contents(id).Remove(inner, dir);
        Note("mods: %s %016llX/%s", ok ? "deleted" : "could not delete", id, inner.c_str());
        return ok;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        std::string inner_from, inner_to;
        unsigned long long id = 0, id_to = 0;
        Read();
        if (Mods(from, inner_from, id) != mtp::ModsPath::Inside || Mods(to, inner_to, id_to) != mtp::ModsPath::Inside || id != id_to) {
            return false;
        }
        const bool ok = Contents(id).Rename(inner_from, inner_to);
        Note("mods: %s %016llX/%s to %s", ok ? "renamed" : "could not rename", id, inner_from.c_str(), inner_to.c_str());
        return ok;
    }

private:
    struct File {
        std::string name;
        InstalledApp::Content content;
    };
    // where a path is as to the folder of mods of its game, with that game's title ID
    mtp::ModsPath Mods(const std::string& path, std::string& inner, unsigned long long& id) {
        std::string game;
        const mtp::ModsPath where = mtp::SplitModsPath(path, game, inner);
        if (where == mtp::ModsPath::No) return where;
        std::lock_guard lock(m_mutex);
        const auto it = m_ids.find(game);
        if (it == m_ids.end()) return mtp::ModsPath::Bad; // not a game: nothing of it is there
        id = it->second;
        return where;
    }
    // a path under the folder of mods of a game, with atmosphere/contents/<title ID>/ made for it
    bool Writable(const std::string& path, std::string& inner, unsigned long long& id) {
        Read();
        if (Mods(path, inner, id) != mtp::ModsPath::Inside) return false;
        mkdir("sdmc:/atmosphere", 0777);
        mkdir("sdmc:/atmosphere/contents", 0777);
        mkdir(ContentsDir(id).c_str(), 0777);
        return true;
    }
    static mtp::FolderStorage Contents(unsigned long long id) { return mtp::FolderStorage(ContentsDir(id), ""); }
    // the list of games once per connection (reading what is installed is slow)
    void Read() {
        std::lock_guard lock(m_mutex);
        if (m_read) return;
        std::function<std::vector<InstalledApp>()> provider;
        {
            std::lock_guard g(g_mutex);
            provider = g_installed;
        }
        if (!provider) return;
        const auto apps = provider();
        if (apps.empty()) return; // still being read: asked again on the next listing
        m_read = true;
        for (const auto& a : apps) {
            if (a.archived || a.contents.empty()) continue;
            std::string folder = Clean(a.name);
            if (m_games.count(folder)) folder += " [" + Hex16(a.app_id) + "]";
            m_ids[folder] = a.app_id;
            auto& files = m_games[folder];
            for (const auto& c : a.contents) {
                char name[400];
                std::snprintf(name, sizeof(name), "%s [%s][v%u].nsp", Clean(a.name).c_str(), Hex16(c.id).c_str(), c.version);
                files.push_back({name, c});
            }
        }
    }
    // the size of an NSP, made the first time it is asked
    uint64_t Size(const std::string& path, const File& f) {
        auto it = m_nsps.find(path);
        if (it == m_nsps.end()) it = m_nsps.emplace(path, OpenInstalledNsp(f.content.id, f.content.version, f.content.type, f.content.nand)).first;
        return it->second ? it->second->size : 0;
    }
    std::string m_description;
    std::mutex m_mutex;
    bool m_read = false;
    std::map<std::string, std::vector<File>> m_games;
    std::map<std::string, unsigned long long> m_ids; // the title ID of each folder
    std::map<std::string, std::shared_ptr<InstalledNsp>> m_nsps;
};

// The saves: a folder for each game, a zip for each user (and the console's own), made on the SD
// card the first time the computer asks for its size, and taken away when MTP stops. A zip with
// the name of one of them dropped on its game restores it (18), as the vault restores a copy of
// the SD card: the save it replaces goes to the SD card first, and back if the restore fails half
// way. Only over a save that is there, never while its game runs; nothing is deleted or renamed.
class SavesStorage final : public Refuse {
public:
    explicit SavesStorage(std::string description) : m_description(std::move(description)) {}
    std::string Description() const override { return m_description; }
    bool ReadOnly() const override { return false; }
    void Space(uint64_t& total, uint64_t& free) override { SdSpace(total, free); } // a zip dropped waits on the SD card
    bool List(const std::string& dir, std::vector<mtp::Entry>& out) override {
        Read();
        std::lock_guard lock(m_mutex);
        if (dir.empty()) {
            for (const auto& [name, saves] : m_games) out.push_back({name, true, 0, 0});
            return true;
        }
        const auto it = m_games.find(dir);
        if (it == m_games.end()) return false;
        for (auto& s : it->second) {
            if (Make(s)) out.push_back({s.name, false, s.size, 0});
        }
        return true;
    }
    bool Stat(const std::string& path, mtp::Entry& out) override {
        Read();
        std::lock_guard lock(m_mutex);
        const auto slash = path.find('/');
        if (slash == std::string::npos) {
            if (!m_games.count(path)) return false;
            out = {path, true, 0, 0};
            return true;
        }
        const auto it = m_games.find(path.substr(0, slash));
        if (it == m_games.end()) return false;
        for (auto& s : it->second) {
            if (s.name == path.substr(slash + 1) && Make(s)) {
                out = {s.name, false, s.size, 0};
                return true;
            }
        }
        return false;
    }
    std::unique_ptr<mtp::Source> Open(const std::string& path) override {
        mtp::Entry e;
        if (!Stat(path, e)) return nullptr;
        std::lock_guard lock(m_mutex);
        const auto slash = path.find('/');
        for (auto& s : m_games[path.substr(0, slash)]) {
            if (s.name == path.substr(slash + 1)) return m_files.Open(s.zip.substr(std::strlen(kZips) + 1));
        }
        return nullptr;
    }
    std::unique_ptr<mtp::Sink> Create(const std::string& path, uint64_t size, bool& busy) override {
        busy = false;
        std::string game, file;
        if (!mtp::SaveDrop(path, game, file)) {
            Note("saves: %s refused: only a zip on the folder of a game restores", path.c_str());
            return nullptr;
        }
        Read();
        Save target;
        int found = 0;
        {
            std::lock_guard lock(m_mutex);
            const auto it = m_games.find(game);
            if (it != m_games.end()) {
                for (const auto& sv : it->second) {
                    if (sv.name == file) {
                        target = sv;
                        found++;
                    }
                }
            }
        }
        if (found != 1) {
            // a save is never made: only one that is there, of a user whose name says which
            Note("saves: %s refused: %s", path.c_str(), found ? "two saves have that name" : "the game has no save with that name");
            return nullptr;
        }
        if (GameRunning(target.app)) {
            Note("saves: %s refused: %016llX is running", path.c_str(), target.app);
            return nullptr;
        }
        mkdir(kTmp, 0777);
        auto inner = m_tmp.Create("mtp-restore.zip", size, busy);
        if (!inner) {
            Note("saves: %s refused: no room on the SD card", path.c_str());
            return nullptr;
        }
        Note("saves: receiving %s (%llu bytes)", path.c_str(), (unsigned long long)size);
        return std::make_unique<RestoreSink>(std::move(inner), target, path, this);
    }

private:
    struct Save {
        std::string name, zip, who;
        unsigned char uid[16]{};
        unsigned long long app = 0;
        int kind = 0;
        uint64_t size = 0;
        bool made = false, failed = false;
    };
    void Read() {
        std::lock_guard lock(m_mutex);
        if (m_read) return;
        m_read = true;
        mkdir(kZips, 0777);
        // the names of the games and of the users
        std::map<unsigned long long, std::string> names;
        {
            std::function<std::vector<InstalledApp>()> provider;
            {
                std::lock_guard g(g_mutex);
                provider = g_installed;
            }
            if (provider) {
                for (const auto& a : provider()) names[a.app_id] = a.name;
            }
        }
        Vault vault;
        const auto users = vault.Users();
        const Tools& tt = TrT();
        FsSaveDataInfoReader reader;
        if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) return;
        FsSaveDataInfo infos[32];
        s64 n = 0;
        while (R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, infos, 32, &n)) && n > 0) {
            for (s64 i = 0; i < n; i++) {
                const auto& info = infos[i];
                if (!info.application_id) continue;
                Save s;
                s.app = info.application_id;
                std::string who;
                if (info.save_data_type == FsSaveDataType_Account) {
                    s.kind = 0;
                    std::memcpy(s.uid, &info.uid, sizeof(s.uid));
                    for (const auto& u : users) {
                        if (!std::memcmp(u.uid, s.uid, sizeof(s.uid))) who = u.name;
                    }
                    if (who.empty()) who = "User";
                } else if (info.save_data_type == FsSaveDataType_Device) {
                    s.kind = 1;
                    who = tt.mu_device;
                } else if (info.save_data_type == FsSaveDataType_Bcat) {
                    s.kind = 2;
                    who = "BCAT";
                } else {
                    continue;
                }
                const auto it = names.find(s.app);
                const std::string game = Clean(it != names.end() ? it->second : Hex16(s.app));
                s.name = mtp::SaveZipName(game, who);
                s.who = who;
                char zip[160];
                std::snprintf(zip, sizeof(zip), "%s/%016llX-%d-%02x%02x%02x%02x.zip", kZips, s.app, s.kind, s.uid[0], s.uid[1], s.uid[2], s.uid[3]);
                s.zip = zip;
                m_games[game].push_back(s);
            }
        }
        fsSaveDataInfoReaderClose(&reader);
    }
    bool Make(Save& s) {
        if (s.made) return true;
        if (s.failed) return false;
        if (!SaveZipTo(s.uid, s.app, s.kind, s.zip)) {
            s.failed = true;
            return false;
        }
        struct stat st{};
        stat(s.zip.c_str(), &st);
        s.size = uint64_t(st.st_size);
        s.made = true;
        return true;
    }
    std::string m_description;
    std::mutex m_mutex;
    bool m_read = false;
    // the zip received, over the save once it is whole
    class RestoreSink final : public mtp::Sink {
    public:
        RestoreSink(std::unique_ptr<mtp::Sink> inner, Save save, std::string path, SavesStorage* owner)
            : m_inner(std::move(inner)), m_save(std::move(save)), m_path(std::move(path)), m_owner(owner) {}
        ~RestoreSink() override {
            if (m_inner) Close(false);
        }
        bool Write(const void* data, size_t size) override { return m_inner->Write(data, size); }
        bool Close(bool ok) override {
            const bool whole = m_inner->Close(ok);
            m_inner.reset();
            const std::string zip = std::string(kTmp) + "/mtp-restore.zip";
            if (!whole) {
                Note("saves: %s did not arrive whole: nothing was restored", m_path.c_str());
                return false;
            }
            if (GameRunning(m_save.app)) {
                Note("saves: %s not restored: %016llX is running", m_path.c_str(), m_save.app);
                std::remove(zip.c_str());
                return false;
            }
            const std::string error = RestoreSdZip(m_save.uid, m_save.who, m_save.kind, m_save.app, zip, TrM().before_restore);
            std::remove(zip.c_str());
            m_owner->Restored(m_save);
            if (error.empty()) Note("saves: %s restored over %016llX (kind %d); the save before is on the SD card", m_path.c_str(), m_save.app, m_save.kind);
            else Note("saves: %s could not be restored over %016llX (kind %d)", m_path.c_str(), m_save.app, m_save.kind);
            return error.empty();
        }

    private:
        std::unique_ptr<mtp::Sink> m_inner;
        Save m_save;
        std::string m_path;
        SavesStorage* m_owner;
    };
    // the zip shown of a save restored is made again when it is asked for
    void Restored(const Save& done) {
        std::lock_guard lock(m_mutex);
        for (auto& [game, saves] : m_games) {
            for (auto& sv : saves) {
                if (sv.zip != done.zip) continue;
                std::remove(sv.zip.c_str());
                sv.made = sv.failed = false;
                sv.size = 0;
            }
        }
    }

    std::map<std::string, std::vector<Save>> m_games;
    mtp::FolderStorage m_files{kZips, ""};
    mtp::FolderStorage m_tmp{kTmp, ""};
};

void RemoveZips() {
    DIR* d = opendir(kZips);
    if (!d) return;
    while (const dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::remove((std::string(kZips) + "/" + e->d_name).c_str());
    }
    closedir(d);
    rmdir(kZips);
}

} // namespace

void MtpSetInstalled(std::function<std::vector<InstalledApp>()> installed) {
    std::lock_guard lock(g_mutex);
    g_installed = std::move(installed);
}

void MtpAddMore(std::vector<std::shared_ptr<mtp::Storage>>& storages, int units) {
    const Tools& tt = TrT();
    if (units & 1) storages.push_back(std::make_shared<InstalledStorage>(tt.mu_installed));
    if (units & 2) storages.push_back(std::make_shared<SavesStorage>(tt.mu_saves));
    if (units & 4) {
        // the captures and videos of the SD card, mounted read only
        FsFileSystem fs;
        if (!g_album && R_SUCCEEDED(fsOpenImageDirectoryFileSystem(&fs, FsImageDirectoryId_Sd))) {
            g_album = fsdevMountDevice("nxalbum", fs) >= 0;
            if (!g_album) fsFsClose(&fs);
        }
        if (g_album) storages.push_back(std::make_shared<ReadOnlyFolder>("nxalbum:/", tt.mu_album));
    }
    if (units & 8) {
        for (const auto& d : UsbDisks()) {
            storages.push_back(std::make_shared<ReadOnlyFolder>(d.mount, std::string(tt.mu_usb) + (d.label.empty() ? "" : " · " + d.label)));
        }
    }
}

void MtpMoreStop() {
    if (g_album) {
        fsdevUnmountDevice("nxalbum");
        g_album = false;
    }
    RemoveZips();
}

} // namespace nxdock
