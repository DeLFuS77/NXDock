// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "catalog.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <yyjson.h>
#include <algorithm>
#include <sys/stat.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "http.hpp"
#include "i18n.hpp"

namespace nxdock {
namespace {

u64 ParseTitleId(const char* hex) {
    return hex ? std::strtoull(hex, nullptr, 16) : 0;
}

std::string Str(yyjson_val* obj, const char* key) {
    const char* s = yyjson_get_str(yyjson_obj_get(obj, key));
    return s ? s : "";
}

// The application a title belongs to: its updates end in 800, its DLC are one block above.
u64 ApplicationOf(u64 title_id, CatalogFile::Kind kind) {
    switch (kind) {
        case CatalogFile::Kind::Update: return title_id & ~0x800ull;
        case CatalogFile::Kind::Dlc: return (title_id - 0x1000) & ~0xFFFull;
        default: return title_id;
    }
}

} // namespace

const CatalogFile* Game::Base() const {
    for (const auto& f : files) {
        if (f.kind == CatalogFile::Kind::Game) return &f;
    }
    return nullptr;
}

const CatalogFile* Game::NewestUpdate() const {
    const CatalogFile* newest = nullptr;
    for (const auto& f : files) {
        if (f.kind == CatalogFile::Kind::Update && (!newest || f.version > newest->version)) newest = &f;
    }
    return newest;
}

std::vector<const CatalogFile*> Game::Dlc() const {
    std::vector<const CatalogFile*> out;
    for (const auto& f : files) {
        if (f.kind == CatalogFile::Kind::Dlc) out.push_back(&f);
    }
    return out;
}

bool Game::UpdateAvailable() const {
    const CatalogFile* update = NewestUpdate();
    return installed.base && update && update->version > installed.patch;
}

bool Game::MissingDlc() const {
    if (!installed.base) return false;
    for (const auto* d : Dlc()) {
        if (!installed.dlc.count(d->title_id)) return true;
    }
    return false;
}

u64 Game::SizeOfAll() const {
    u64 total = 0;
    if (const auto* b = Base()) total += b->size;
    if (const auto* u = NewestUpdate()) total += u->size;
    for (const auto* d : Dlc()) total += d->size;
    return total;
}

bool ParseCatalog(const std::string& json, std::vector<Game>& out) {
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) {
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* games = yyjson_obj_get(root, "games");
    if (!yyjson_is_arr(games)) {
        yyjson_doc_free(doc);
        return false;
    }
    out.clear();
    size_t i, max;
    yyjson_val* g;
    yyjson_arr_foreach(games, i, max, g) {
        Game game;
        game.key = Str(g, "key");
        game.name = Str(g, "name");
        game.cover = Str(g, "cover");
        game.added = (long long)yyjson_get_uint(yyjson_obj_get(g, "added"));
        game.title_id = ParseTitleId(yyjson_get_str(yyjson_obj_get(g, "titleId")));
        size_t j, jmax;
        yyjson_val* f;
        yyjson_arr_foreach(yyjson_obj_get(g, "files"), j, jmax, f) {
            CatalogFile file;
            file.id = Str(f, "id");
            file.name = Str(f, "name");
            file.dlc_name = Str(f, "dlcName");
            file.format = Str(f, "format");
            file.url = Str(f, "url");
            const std::string kind = Str(f, "kind");
            file.kind = kind == "update" ? CatalogFile::Kind::Update : kind == "dlc" ? CatalogFile::Kind::Dlc : CatalogFile::Kind::Game;
            file.title_id = ParseTitleId(yyjson_get_str(yyjson_obj_get(f, "titleId")));
            file.version = u32(yyjson_get_uint(yyjson_obj_get(f, "version")));
            file.size = yyjson_get_uint(yyjson_obj_get(f, "size"));
            file.required_system = u32(yyjson_get_uint(yyjson_obj_get(f, "requiredSystem")));
            if (!file.id.empty() && !file.url.empty()) {
                game.files.push_back(std::move(file));
            }
        }
        if (game.title_id == 0) {
            // a game whose base is not in the library: its application from an update or a DLC
            for (const auto& file : game.files) {
                if (file.title_id) {
                    game.title_id = ApplicationOf(file.title_id, file.kind);
                    break;
                }
            }
        }
        if (!game.files.empty()) {
            out.push_back(std::move(game));
        }
    }
    yyjson_doc_free(doc);
    return true;
}

bool ParsePush(const std::string& json, std::vector<PushItem>& out) {
    out.clear();
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) {
        return false;
    }
    yyjson_val* items = yyjson_obj_get(yyjson_doc_get_root(doc), "items");
    if (!yyjson_is_arr(items)) {
        yyjson_doc_free(doc);
        return false;
    }
    size_t i, max;
    yyjson_val* v;
    yyjson_arr_foreach(items, i, max, v) {
        PushItem p;
        p.id = Str(v, "id");
        p.file_id = Str(v, "fileId");
        p.url = Str(v, "url");
        p.game = Str(v, "game");
        p.kind = Str(v, "kind");
        p.format = Str(v, "format");
        p.name = Str(v, "name");
        p.from = Str(v, "from");
        p.title_id = ParseTitleId(yyjson_get_str(yyjson_obj_get(v, "titleId")));
        p.version = u32(yyjson_get_uint(yyjson_obj_get(v, "version")));
        p.size = yyjson_get_uint(yyjson_obj_get(v, "size"));
        // only a file of the app: a path on it, never another address
        if (!p.id.empty() && p.url.size() > 1 && p.url[0] == '/' && p.url[1] != '/') {
            out.push_back(std::move(p));
        }
    }
    yyjson_doc_free(doc);
    return true;
}

std::string InventoryJson(const std::vector<InstalledContent>& items) {
    std::string out = "{\"items\":[";
    char entry[160];
    for (size_t i = 0; i < items.size(); i++) {
        const auto& c = items[i];
        const char* type = c.type == 0x81 ? "update" : c.type == 0x82 ? "dlc" : "game";
        std::snprintf(entry, sizeof(entry), "%s{\"titleId\":\"%016llX\",\"type\":\"%s\",\"version\":%u,\"storage\":\"%s\"}",
                      i ? "," : "", (unsigned long long)c.title_id, type, c.version, c.nand ? "nand" : "sd");
        out += entry;
    }
    return out + "]}";
}

#ifdef __SWITCH__
// What the console has, by application. A record of ns is not enough: ns keeps the record and
// its metas of an archived game, or of one whose contents were removed, so a content counts only
// when the meta database of its storage (SD card or internal memory) holds it.
std::map<u64, InstalledState> ReadInstalled(std::vector<InstalledContent>* list) {
    std::map<u64, InstalledState> installed;
    if (R_FAILED(nsInitialize())) {
        return installed;
    }
    const bool ncm = R_SUCCEEDED(ncmInitialize());
    NcmContentMetaDatabase sd_db{}, nand_db{};
    const bool sd_open = ncm && R_SUCCEEDED(ncmOpenContentMetaDatabase(&sd_db, NcmStorageId_SdCard));
    const bool nand_open = ncm && R_SUCCEEDED(ncmOpenContentMetaDatabase(&nand_db, NcmStorageId_BuiltInUser));
    // the contents themselves: another tool may remove them and leave their meta behind
    NcmContentStorage sd_cs{}, nand_cs{};
    const bool sd_cs_open = sd_open && R_SUCCEEDED(ncmOpenContentStorage(&sd_cs, NcmStorageId_SdCard));
    const bool nand_cs_open = nand_open && R_SUCCEEDED(ncmOpenContentStorage(&nand_cs, NcmStorageId_BuiltInUser));
    const auto present = [&](const NsApplicationContentMetaStatus& m) {
        NcmContentMetaDatabase* db = nullptr;
        if (m.storageID == NcmStorageId_SdCard && sd_open) db = &sd_db;
        if (m.storageID == NcmStorageId_BuiltInUser && nand_open) db = &nand_db;
        if (!sd_open && !nand_open) {
            // ncm did not answer: fall back to what ns says
            return m.storageID == NcmStorageId_SdCard || m.storageID == NcmStorageId_BuiltInUser;
        }
        if (!db) {
            return false; // no storage (archived), a game card, or a database that did not open
        }
        NcmContentMetaKey key{};
        key.id = m.application_id;
        key.version = m.version;
        key.type = m.meta_type;
        key.install_type = NcmContentInstallType_Full;
        bool has = false;
        if (R_FAILED(ncmContentMetaDatabaseHas(db, &has, &key)) || !has) return false;
        // a meta whose program (or data, for a DLC) is gone from the storage is not installed
        NcmContentStorage* cs = db == &sd_db ? (sd_cs_open ? &sd_cs : nullptr) : (nand_cs_open ? &nand_cs : nullptr);
        if (!cs) return true;
        NcmContentId id{};
        const NcmContentType type = m.meta_type == NcmContentMetaType_AddOnContent ? NcmContentType_Data : NcmContentType_Program;
        if (R_FAILED(ncmContentMetaDatabaseGetContentIdByType(db, &id, &key, type))) return true; // not told: as it was
        bool there = false;
        return R_FAILED(ncmContentStorageHas(cs, &there, &id)) || there;
    };
    std::vector<NsApplicationRecord> records(64);
    s32 offset = 0;
    while (true) {
        s32 count = 0;
        if (R_FAILED(nsListApplicationRecord(records.data(), s32(records.size()), offset, &count)) || count <= 0) {
            break;
        }
        for (s32 i = 0; i < count; i++) {
            const u64 app = records[i].application_id;
            InstalledState state;
            NsApplicationContentMetaStatus metas[32];
            s32 metas_count = 0;
            if (R_SUCCEEDED(nsListApplicationContentMetaStatus(app, 0, metas, 32, &metas_count))) {
                for (s32 m = 0; m < metas_count; m++) {
                    if (!present(metas[m])) {
                        continue;
                    }
                    if (list && (metas[m].meta_type == NcmContentMetaType_Application || metas[m].meta_type == NcmContentMetaType_Patch ||
                                 metas[m].meta_type == NcmContentMetaType_AddOnContent)) {
                        list->push_back({metas[m].application_id, metas[m].meta_type, metas[m].version,
                                         metas[m].storageID == NcmStorageId_BuiltInUser});
                    }
                    switch (metas[m].meta_type) {
                        case NcmContentMetaType_Application:
                            state.base = true;
                            state.base_nand = metas[m].storageID == NcmStorageId_BuiltInUser;
                            break;
                        case NcmContentMetaType_Patch: state.patch = std::max(state.patch, metas[m].version); break;
                        case NcmContentMetaType_AddOnContent: state.dlc.insert(metas[m].application_id); break;
                        default: break;
                    }
                }
            }
            if (state.base || state.patch || !state.dlc.empty()) {
                installed[app] = state;
            }
        }
        offset += count;
        if (count < s32(records.size())) {
            break;
        }
    }
    if (sd_cs_open) ncmContentStorageClose(&sd_cs);
    if (nand_cs_open) ncmContentStorageClose(&nand_cs);
    if (sd_open) ncmContentMetaDatabaseClose(&sd_db);
    if (nand_open) ncmContentMetaDatabaseClose(&nand_db);
    if (ncm) ncmExit();
    nsExit();
    return installed;
}
#endif

#ifdef __SWITCH__
namespace {

// The last catalog of the library, kept on the SD card: shown at once the next time, while the
// library gives the new one. The first line is the address it came from.
constexpr const char* kCatalogCache = "sdmc:/switch/NXDock/catalog-cache.json";
constexpr size_t kCatalogMax = 128u * 1024 * 1024;

bool ReadCatalogCache(const std::string& url, std::string& body) {
    FILE* f = std::fopen(kCatalogCache, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bool ok = size > 0 && size_t(size) < kCatalogMax;
    std::string all;
    if (ok) {
        all.resize(size_t(size));
        ok = std::fread(all.data(), 1, all.size(), f) == all.size();
    }
    std::fclose(f);
    const size_t nl = all.find('\n');
    if (!ok || nl == std::string::npos || all.compare(0, nl, url) != 0) return false;
    body = all.substr(nl + 1);
    return true;
}

void WriteCatalogCache(const std::string& url, const std::string& body) {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/NXDock", 0777);
    const std::string temp = std::string(kCatalogCache) + ".tmp";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return;
    bool ok = std::fwrite(url.data(), 1, url.size(), f) == url.size() && std::fputc('\n', f) != EOF &&
              std::fwrite(body.data(), 1, body.size(), f) == body.size();
    ok = std::fclose(f) == 0 && ok;
    if (ok) {
        std::remove(kCatalogCache);
        std::rename(temp.c_str(), kCatalogCache);
    } else {
        std::remove(temp.c_str());
    }
}

} // namespace

Catalog::~Catalog() {
    m_cancel = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void Catalog::Load(const std::string& url, const std::string& key) {
    if (m_state == State::Loading) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_cancel = false;
    m_preparing = false;
    m_state = State::Loading;
    m_thread = std::thread(&Catalog::Run, this, url, key);
}

std::string Catalog::Error() {
    std::lock_guard lock(m_mutex);
    return m_error;
}

std::vector<Game> Catalog::Games() {
    std::lock_guard lock(m_mutex);
    return m_games;
}

void Catalog::Publish(std::vector<Game> games) {
    const auto installed = ReadInstalled();
    for (auto& game : games) {
        const auto it = installed.find(game.title_id);
        if (it != installed.end()) {
            game.installed = it->second;
        }
    }
    {
        std::lock_guard lock(m_mutex);
        m_games = std::move(games);
    }
    m_version++;
}

void Catalog::Run(std::string url, std::string key) {
    // 1. the list kept on the SD card, at once
    bool shown = false;
    {
        std::string cached;
        std::vector<Game> games;
        if (ReadCatalogCache(url, cached) && ParseCatalog(cached, games) && !games.empty()) {
            Publish(std::move(games));
            shown = true;
            m_stale = true;
        }
    }
    const auto fail = [&](std::string error) {
        m_preparing = false;
        {
            std::lock_guard lock(m_mutex);
            m_error = std::move(error);
        }
        // with the kept list on screen, it stays usable; the error only tells it is not new
        m_state = shown ? State::Ready : State::Failed;
    };

    // 2. the catalog of the library; a large one may first be prepared: the app says so with
    // 503 and Retry-After, and is asked again until it is ready
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    while (!m_cancel) {
        const auto r = http::GetLarge(url + "/install/catalog?ncz=native", key, 180000, kCatalogMax, &m_cancel);
        if (m_cancel) break;
        if (r.status == 503 && http::JsonBool(r.body, "preparing")) {
            m_done = int(http::JsonNumber(r.body, "done", 0));
            m_total = int(http::JsonNumber(r.body, "total", 0));
            m_preparing = true;
            if (std::chrono::steady_clock::now() > deadline) {
                fail(Tr().err_connect);
                return;
            }
            const long wait_ms = std::clamp(r.retry_after > 0 ? r.retry_after : 5L, 2L, 15L) * 1000;
            for (long waited = 0; waited < wait_ms && !m_cancel; waited += 100) {
                svcSleepThread(100'000'000);
            }
            continue;
        }
        m_preparing = false;
        std::vector<Game> games;
        if (r.status == 200 && ParseCatalog(r.body, games)) {
            WriteCatalogCache(url, r.body);
            Publish(std::move(games));
            {
                std::lock_guard lock(m_mutex);
                m_error.clear();
            }
            m_stale = false;
            m_state = State::Ready;
            return;
        }
        fail(r.status == 401 ? Tr().err_key : r.status == 404 ? Tr().err_off
             : r.status == 0 ? std::string(Tr().err_connect) + " (" + r.error + ")" : "HTTP " + std::to_string(r.status));
        return;
    }
    fail(Tr().cancelled);
}

#endif

} // namespace nxdock
