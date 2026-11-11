// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// More folders for the computer over MTP (8A of the visual test of the new tools, 11/11), all read
// only: the games installed, each a folder with its game, update and DLC as NSP made as they are
// read (copying one to the computer dumps it, with its common ticket); the saves, a zip for each
// game and user (and the console's own); the Album; and a USB disk plugged in. Nothing on the
// console is written or changed from them.
#include "mtp_more.hpp"

#include "i18n_tools.hpp"
#include "mtp_storage.hpp"
#include "usb_disks.hpp"
#include "vault.hpp"

#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace nxdock {
namespace {

constexpr const char* kZips = "sdmc:/switch/NXDock/mtp-saves";

std::mutex g_mutex;
std::function<std::vector<InstalledApp>()> g_installed;
bool g_album = false;

// A name a computer takes for a file or folder.
std::string Clean(const std::string& name) {
    std::string out;
    for (const char c : name) out += std::string("/\\:*?\"<>|").find(c) == std::string::npos && (unsigned char)c >= 0x20 ? c : '_';
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? "_" : out;
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

// The games installed: a folder each, an NSP for each of its contents.
class InstalledStorage final : public Refuse {
public:
    explicit InstalledStorage(std::string description) : m_description(std::move(description)) {}
    std::string Description() const override { return m_description; }
    void Space(uint64_t& total, uint64_t& free) override {
        total = free = 0;
        NcmContentStorage cs{};
        if (R_SUCCEEDED(ncmOpenContentStorage(&cs, NcmStorageId_SdCard))) {
            s64 t = 0;
            ncmContentStorageGetTotalSpaceSize(&cs, &t);
            total = uint64_t(t);
            ncmContentStorageClose(&cs);
        }
    }
    bool List(const std::string& dir, std::vector<mtp::Entry>& out) override {
        Read();
        std::lock_guard lock(m_mutex);
        if (dir.empty()) {
            for (const auto& [name, game] : m_games) out.push_back({name, true, 0, 0});
            return true;
        }
        const auto it = m_games.find(dir);
        if (it == m_games.end()) return false;
        for (const auto& f : it->second) out.push_back({f.name, false, Size(dir + "/" + f.name, f), 0});
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
        for (const auto& f : it->second) {
            if (f.name == path.substr(slash + 1)) {
                out = {f.name, false, Size(path, f), 0};
                return true;
            }
        }
        return false;
    }
    std::unique_ptr<mtp::Source> Open(const std::string& path) override {
        mtp::Entry e;
        if (!Stat(path, e)) return nullptr;
        std::lock_guard lock(m_mutex);
        const auto it = m_nsps.find(path);
        if (it == m_nsps.end() || !it->second) return nullptr;
        return std::make_unique<NspSource>(it->second);
    }

private:
    struct File {
        std::string name;
        InstalledApp::Content content;
    };
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
    std::map<std::string, std::shared_ptr<InstalledNsp>> m_nsps;
};

// The saves: a folder for each game, a zip for each user (and the console's own), made on the SD
// card the first time the computer asks for its size, and taken away when MTP stops.
class SavesStorage final : public Refuse {
public:
    explicit SavesStorage(std::string description) : m_description(std::move(description)) {}
    std::string Description() const override { return m_description; }
    void Space(uint64_t& total, uint64_t& free) override { total = free = 0; }
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

private:
    struct Save {
        std::string name, zip;
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
                s.name = Clean(game + " · " + who) + ".zip";
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
    std::map<std::string, std::vector<Save>> m_games;
    mtp::FolderStorage m_files{kZips, ""};
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
