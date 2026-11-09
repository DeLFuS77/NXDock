// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "catalog.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <yyjson.h>
#include <algorithm>
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

#ifdef __SWITCH__
std::map<u64, InstalledState> ReadInstalled() {
    std::map<u64, InstalledState> installed;
    if (R_FAILED(nsInitialize())) {
        return installed;
    }
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
                    switch (metas[m].meta_type) {
                        case NcmContentMetaType_Application: state.base = true; break;
                        case NcmContentMetaType_Patch: state.patch = std::max(state.patch, metas[m].version); break;
                        case NcmContentMetaType_AddOnContent: state.dlc.insert(metas[m].application_id); break;
                        default: break;
                    }
                }
            }
            installed[app] = state;
        }
        offset += count;
        if (count < s32(records.size())) {
            break;
        }
    }
    nsExit();
    return installed;
}
#endif

#ifdef __SWITCH__
Catalog::~Catalog() {
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

void Catalog::Run(std::string url, std::string key) {
    const auto r = http::GetLarge(url + "/install/catalog", key, 20000, 16 * 1024 * 1024);
    std::vector<Game> games;
    if (r.status != 200 || !ParseCatalog(r.body, games)) {
        std::lock_guard lock(m_mutex);
        m_error = r.status == 401 ? Tr().err_key : r.status == 404 ? Tr().err_off
                : r.status == 0 ? std::string(Tr().err_connect) + " (" + r.error + ")" : "HTTP " + std::to_string(r.status);
        m_state = State::Failed;
        return;
    }
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
        m_error.clear();
    }
    m_version++;
    m_state = State::Ready;
}

#endif

} // namespace nxdock
