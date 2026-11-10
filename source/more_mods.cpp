// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Cheats and mods (10A of the visual test of phases 9 to 15): a game is chosen from what is
// installed; its page has the cheats of the library for the version installed on the left and
// the mod packages of the SD card on the right, each with a switch.
#include "app.hpp"

#include "draw.hpp"
#include "errors.hpp"
#include "http.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"

#include <yyjson.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>

namespace nxdock {
namespace {

std::string Hex(unsigned long long id) {
    char t[20];
    std::snprintf(t, sizeof(t), "%016llX", id);
    return t;
}

std::string CheatFile(unsigned long long id, const std::string& build) {
    return ContentsDir(id) + "/cheats/" + build + ".txt";
}

bool Exists(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

void MakeDirs(const std::string& file) {
    for (size_t i = file.find('/', file.find(":/") + 2); i != std::string::npos; i = file.find('/', i + 1)) {
        mkdir(file.substr(0, i).c_str(), 0777);
    }
}

} // namespace

App::Cheats::~Cheats() {
    if (thread.joinable()) thread.join();
}

void App::OpenMods() {
    if (!m_space) m_space = std::make_unique<Space>();
    m_space->StartScan();
    m_mods_game = -1;
    m_mods_pick = 0;
    m_more = MoreScreen::Mods;
}

void App::OpenModsGame(unsigned long long app_id, const std::string& name) {
    m_mods_app = app_id;
    m_mods_name = name;
    m_mods_game = 1;
    m_mods_side = 0;
    m_mods_focus = 0;
    m_mods = ModPackages(ModsDir(), ModsStateDir(), app_id);
    // the version installed: its update, or the game
    m_mods_version = 0;
    if (m_queue) {
        const auto installed = m_queue->Installed();
        const auto it = installed.find(app_id);
        if (it != installed.end()) m_mods_version = it->second.patch;
    }
    // the cheats of the library, on a thread
    if (m_cheats.thread.joinable()) m_cheats.thread.join();
    {
        std::lock_guard lock(m_cheats.mutex);
        m_cheats.done = false;
        m_cheats.found = false;
        m_cheats.names.clear();
        m_cheats.build.clear();
        m_cheats.text.clear();
    }
    const Config c = m_link.CurrentConfig();
    if (!c.Linked()) {
        m_cheats.done = true;
        return;
    }
    const std::string url = c.url + "/install/cheats/" + Hex(app_id) + "?version=" + std::to_string(m_mods_version);
    const std::string key = c.key;
    m_cheats.thread = std::thread([this, url, key] {
        const auto r = http::GetLarge(url, key, 10000, 4 * 1024 * 1024);
        std::lock_guard lock(m_cheats.mutex);
        m_cheats.done = true;
        if (r.status != 200) return;
        yyjson_doc* doc = yyjson_read(r.body.data(), r.body.size(), 0);
        if (!doc) return;
        yyjson_val* root = yyjson_doc_get_root(doc);
        const char* build = yyjson_get_str(yyjson_obj_get(root, "build_id"));
        const char* text = yyjson_get_str(yyjson_obj_get(root, "text"));
        size_t i, n;
        yyjson_val* v;
        yyjson_arr_foreach(yyjson_obj_get(root, "names"), i, n, v) {
            if (yyjson_get_str(v)) m_cheats.names.push_back(yyjson_get_str(v));
        }
        if (build && text && std::string(build).size() == 16) {
            m_cheats.build = build;
            for (auto& ch : m_cheats.build) ch = char(std::toupper((unsigned char)ch));
            m_cheats.text = text;
            m_cheats.found = true;
        }
        yyjson_doc_free(doc);
    });
}

void App::DrawMods(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    if (m_mods_game < 0) {
        // the games installed
        const auto v = m_space ? m_space->Snapshot() : Space::View{};
        ui::Text(vg, 48, 104, 22, t.text, m.cm_pick);
        std::vector<int> rows;
        for (int i = 0; i < int(v.apps.size()); i++) {
            if (!v.apps[size_t(i)].archived) rows.push_back(i);
        }
        if (!v.scanned) ui::Text(vg, 48, 140, 15, t.muted, m.scanning);
        m_mods_pick = std::clamp(m_mods_pick, 0, std::max(0, int(rows.size()) - 1));
        const int first = std::max(0, m_mods_pick - 8);
        for (int r = 0; r < 9 && first + r < int(rows.size()); r++) {
            const auto& a = v.apps[size_t(rows[size_t(first + r)])];
            const float y = 130 + r * 52;
            if (first + r == m_mods_pick) {
                ui::FillRound(vg, 40, y, kWidth - 80, 46, 10, t.panel2);
                ui::Focus(vg, t, 40, y, kWidth - 80, 46, 10, m_time);
            }
            AddHit(40, y, kWidth - 80, 46, HitKind::PickRow, first + r);
            const bool cheats = Exists(ContentsDir(a.app_id) + "/cheats");
            ui::Text(vg, 60, y + 23, 16, t.text, a.name.c_str());
            if (cheats) ui::Text(vg, kWidth - 60, y + 23, 14, t.work, m.t_mods, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        std::vector<ui::Hint> hints = {{ui::glyph::A, s.select, HidNpadButton_A}, {ui::glyph::B, s.back, HidNpadButton_B}};
        ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
        return;
    }

    ui::Text(vg, 48, 100, 22, t.text, m_mods_name.c_str());
    const float lx = 48, rx = 664, cw = 568;
    // the cheats
    char head[96];
    std::snprintf(head, sizeof(head), m.cm_cheats_head_fmt, m_mods_version);
    draw::Section(vg, t, lx, 138, head);
    bool found, done;
    std::string build;
    int count;
    {
        std::lock_guard lock(m_cheats.mutex);
        found = m_cheats.found;
        done = m_cheats.done;
        build = m_cheats.build;
        count = int(m_cheats.names.size());
    }
    const bool linked = m_link.CurrentConfig().Linked();
    {
        const float y = 156;
        draw::Panel(vg, t, lx, y, cw, 64, 12);
        if (m_mods_side == 0) ui::Focus(vg, t, lx, y, cw, 64, 12, m_time);
        AddHit(lx, y, cw, 64, HitKind::ModRow, 0);
        ui::Text(vg, lx + 18, y + 22, 17, t.text, m.cm_cheats_lib);
        char sub[160];
        if (!linked) std::snprintf(sub, sizeof(sub), "%s", m.need_link);
        else if (!done) std::snprintf(sub, sizeof(sub), "%s", m.cm_loading);
        else if (!found) std::snprintf(sub, sizeof(sub), "%s", m.cm_no_cheats);
        else std::snprintf(sub, sizeof(sub), m.cm_cheats_count_fmt, count);
        ui::Text(vg, lx + 18, y + 44, 13, t.muted, sub);
        if (found) draw::Toggle(vg, t, lx + cw - 72, y + 32, Exists(CheatFile(m_mods_app, build)));
        if (found) {
            std::lock_guard lock(m_cheats.mutex);
            for (int i = 0; i < int(m_cheats.names.size()) && i < 7; i++) {
                ui::Text(vg, lx + 18, y + 90 + i * 24, 14, t.muted, ("• " + m_cheats.names[size_t(i)]).c_str());
            }
        }
    }
    // the mods
    draw::Section(vg, t, rx, 138, m.cm_mods_head);
    if (m_mods.empty()) draw::Box(vg, rx, 160, cw, 15, t.muted, m.cm_no_mods);
    for (int i = 0; i < int(m_mods.size()) && i < 6; i++) {
        const auto& p = m_mods[size_t(i)];
        const float y = 156 + i * 72;
        draw::Panel(vg, t, rx, y, cw, 64, 12);
        if (m_mods_side == 1 && m_mods_focus == i) ui::Focus(vg, t, rx, y, cw, 64, 12, m_time);
        AddHit(rx, y, cw, 64, HitKind::ModRow, 100 + i);
        nvgSave(vg);
        nvgScissor(vg, rx + 18, y, cw - 110, 64);
        ui::Text(vg, rx + 18, y + 22, 17, t.text, p.name.c_str());
        char sub[96];
        std::snprintf(sub, sizeof(sub), m.cm_mod_files_fmt, p.files, draw::Size(p.bytes).c_str());
        ui::Text(vg, rx + 18, y + 44, 13, t.muted, sub);
        nvgRestore(vg);
        draw::Toggle(vg, t, rx + cw - 72, y + 32, p.on);
    }
    draw::Box(vg, lx, 600, kWidth - 96, 14, t.muted, m.cm_note);
    std::vector<ui::Hint> hints = {{ui::glyph::A, m.cm_toggle, HidNpadButton_A}, {ui::glyph::B, s.back, HidNpadButton_B}};
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::UpdateMods(u64 down, u64 held) {
    const More& m = TrM();
    if (m_mods_game < 0) {
        const auto v = m_space ? m_space->Snapshot() : Space::View{};
        std::vector<int> rows;
        for (int i = 0; i < int(v.apps.size()); i++) {
            if (!v.apps[size_t(i)].archived) rows.push_back(i);
        }
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_mods_pick > 0) m_mods_pick--;
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_mods_pick < int(rows.size()) - 1) m_mods_pick++;
        if (down & HidNpadButton_B) m_more = MoreScreen::Tiles;
        if ((down & HidNpadButton_A) && m_mods_pick < int(rows.size())) {
            const auto& a = v.apps[size_t(rows[size_t(m_mods_pick)])];
            OpenModsGame(a.app_id, a.name);
        }
        return;
    }
    if (down & HidNpadButton_B) {
        m_mods_game = -1;
        return;
    }
    if (Repeat(down, held, HidNpadButton_AnyLeft)) m_mods_side = 0;
    if (Repeat(down, held, HidNpadButton_AnyRight) && !m_mods.empty()) m_mods_side = 1;
    if (m_mods_side == 1) {
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_mods_focus > 0) m_mods_focus--;
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_mods_focus < std::min(6, int(m_mods.size())) - 1) m_mods_focus++;
    }
    if (!(down & HidNpadButton_A)) return;
    const auto fail = [&](const char* title, const char* todo, const std::string& what) {
        ErrorText e{title, what, todo, {}};
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::VaultError;
        m_dialog.single = true;
        m_dialog.from = PackError(e);
        m_dialog.text = m.t_mods;
    };
    if (m_mods_side == 0) {
        std::string build, text;
        {
            std::lock_guard lock(m_cheats.mutex);
            if (!m_cheats.found) return;
            build = m_cheats.build;
            text = m_cheats.text;
        }
        const std::string path = CheatFile(m_mods_app, build);
        if (Exists(path)) {
            std::remove(path.c_str());
            Notice(m.cm_removed);
            return;
        }
        MakeDirs(path);
        FILE* f = std::fopen(path.c_str(), "wb");
        const bool ok = f && std::fwrite(text.data(), 1, text.size(), f) == text.size();
        if (f) std::fclose(f);
        if (!ok) {
            std::remove(path.c_str());
            fail(m.cm_failed_t, m.cm_failed_d, path);
            return;
        }
        Notice(m.cm_put);
        return;
    }
    if (m_mods_focus >= int(m_mods.size())) return;
    const auto& p = m_mods[size_t(m_mods_focus)];
    if (p.on) {
        if (!RemoveMod(p, m_mods_app, ContentsDir(m_mods_app), ModsStateDir())) fail(m.cm_failed_t, m.cm_failed_d, p.name);
        else Notice(m.cm_removed);
    } else {
        std::string conflict;
        if (!InstallMod(p, m_mods_app, ContentsDir(m_mods_app), ModsStateDir(), conflict)) {
            if (!conflict.empty()) {
                fail(m.cm_conflict_t, m.cm_conflict_d, conflict);
            } else {
                // what was written goes again
                RemoveMod(p, m_mods_app, ContentsDir(m_mods_app), ModsStateDir());
                fail(m.cm_failed_t, m.cm_failed_d, p.name);
            }
        } else {
            Notice(m.cm_put);
        }
    }
    m_mods = ModPackages(ModsDir(), ModsStateDir(), m_mods_app);
}

} // namespace nxdock
