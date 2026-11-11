// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// In the console (1A and 2A of the visual test of 11/11): every game installed on the console, also
// those not in the library (installed with DBI, from the eShop or a game card), each with its icon
// of the console; and the page of a game with what it has (the game, its update, each DLC, told
// apart), where what goes is marked and taken off after a question. The saves are never touched.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"

#include <algorithm>
#include <cstdio>

namespace nxdock {

std::string Initial(const std::string& name);
unsigned PlaceholderColor(const std::string& key);

namespace {

const char* Glyphs() {
    static const std::string glyphs = std::string(ui::glyph::ZL) + " " + ui::glyph::ZR;
    return glyphs.c_str();
}

std::string IconPath(unsigned long long app) {
    char path[24];
    std::snprintf(path, sizeof(path), "ns:%016llX", app);
    return path;
}

// What a game has, in order: the game, its update, then its DLC by their IDs.
std::vector<InstalledApp::Content> Items(const InstalledApp& a) {
    auto items = a.contents;
    const auto rank = [](unsigned char type) {
        return type == NcmContentMetaType_Application ? 0 : type == NcmContentMetaType_Patch ? 1 : 2;
    };
    std::stable_sort(items.begin(), items.end(), [&](const InstalledApp::Content& x, const InstalledApp::Content& y) {
        if (rank(x.type) != rank(y.type)) return rank(x.type) < rank(y.type);
        return x.id < y.id;
    });
    return items;
}

// The version of the game the console runs: the newest of the game and its updates.
unsigned GameVersion(const InstalledApp& a) {
    unsigned v = 0;
    for (const auto& c : a.contents) {
        if (c.type == NcmContentMetaType_Application || c.type == NcmContentMetaType_Patch) v = std::max(v, c.version);
    }
    return v;
}

bool NeedsNewer(const InstalledApp& a, const InstalledApp::Content& c) {
    return c.type == NcmContentMetaType_AddOnContent && c.required > GameVersion(a);
}

// A small outlined tag after a text; returns where the next one goes.
void Paragraph(NVGcontext* vg, float x, float y, float w, float size, NVGcolor color, const char* str) {
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.3f);
    nvgFillColor(vg, color);
    nvgTextBox(vg, x, y, w, str, nullptr);
}

float Tag(NVGcontext* vg, float x, float cy, const char* text, NVGcolor color) {
    const float w = ui::TextWidth(vg, 12, text) + 14;
    ui::StrokeRound(vg, x, cy - 9, w, 18, 9, color, 1.f);
    ui::Text(vg, x + w / 2, cy, 12, color, text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    return x + w + 8;
}

const char* WhereOf(const InstalledApp& a, const Strings& s) {
    static std::string both;
    both = std::string("SD + ") + s.internal_short;
    if (a.nand_bytes && a.sd_bytes) return both.c_str();
    return a.nand_bytes ? s.internal_short : "SD";
}

} // namespace

const Game* App::LibraryGame(unsigned long long app) const {
    for (const auto& g : m_games) {
        if (g.title_id == app) return &g;
    }
    return nullptr;
}

std::string App::ContentLabel(const InstalledApp::Content& c, unsigned long long app) const {
    const auto& s = Tr();
    const More& m = TrM();
    char text[200];
    if (c.type == NcmContentMetaType_Application) {
        std::snprintf(text, sizeof(text), "%s · v%u", m.game_word, c.version);
        return text;
    }
    if (c.type == NcmContentMetaType_Patch) {
        std::snprintf(text, sizeof(text), "%s · v%u", m.ic_update_word, c.version);
        return text;
    }
    // a DLC: its name from the library when it is there, else its number
    std::string name;
    if (const Game* g = LibraryGame(app)) {
        for (const auto& f : g->files) {
            if (f.kind == CatalogFile::Kind::Dlc && f.title_id == c.id) {
                name = !f.dlc_name.empty() ? f.dlc_name : f.name;
                break;
            }
        }
    }
    if (name.empty()) {
        std::snprintf(text, sizeof(text), "%s%llu", s.dlc_prefix, (unsigned long long)(c.id & 0xFFF));
        name = text;
    }
    return std::string(m.ic_dlc_word) + " · " + name;
}

// What the console has is read when the view opens, and again when the queue ends a job.
void App::ReadConsole() {
    if (m_installed_view != 0) return;
    if (!m_space) m_space = std::make_unique<Space>();
    const unsigned version = m_queue ? m_queue->Version() : 0;
    if (m_console_scanned_for != version && !m_space->Busy() && m_space->StartScan()) m_console_scanned_for = version;
}

void App::DrawConsole(NVGcontext* vg, const Space::View& v) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    FollowSpace();
    if (m_console_app) {
        DrawConsolePage(vg, v);
        return;
    }
    if (!v.scanned) {
        ui::Spinner(vg, kWidth / 2, 320, 14, t.work, m_time);
        ui::Text(vg, kWidth / 2, 366, 18, t.muted, m.ic_reading, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {{Glyphs(), s.view_switch, HidNpadButton_ZR}, {ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    if (v.apps.empty()) {
        ui::Text(vg, kWidth / 2, 340, 20, t.muted, m.ic_empty, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {{Glyphs(), s.view_switch, HidNpadButton_ZR}, {ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    const bool library = m_catalog.GetState() == Catalog::State::Ready;
    m_console_focus = std::clamp(m_console_focus, 0, int(v.apps.size()) - 1);
    const float top = 148, pitch = 72;
    const int visible = 7;
    const int first = std::clamp(m_console_focus - visible / 2, 0, std::max(0, int(v.apps.size()) - visible));
    for (int r = first; r < int(v.apps.size()) && r < first + visible; r++) {
        const InstalledApp& a = v.apps[size_t(r)];
        const float y = top + (r - first) * pitch;
        if (r == m_console_focus) {
            ui::FillRound(vg, 48, y, kWidth - 96, pitch - 6, 12, t.panel2);
            ui::Focus(vg, t, 48, y, kWidth - 96, pitch - 6, 12, m_time);
        }
        AddHit(48, y, kWidth - 96, pitch - 6, HitKind::InstalledRow, r);
        const Game* g = LibraryGame(a.app_id);
        int image = g && !g->cover.empty() ? m_covers.Get(g->cover) : 0;
        if (!image) image = m_covers.Get(IconPath(a.app_id));
        nvgGlobalAlpha(vg, a.archived ? 0.55f : 1.f);
        ui::Cover(vg, t, 62, y + 5, 56, PlaceholderColor(a.name), Initial(a.name).c_str(), nullptr, false, false, 9, image);
        nvgSave(vg);
        nvgScissor(vg, 134, y, 920, pitch);
        float x = ui::Text(vg, 134, y + 23, 18, t.text, a.name.c_str()) + 12;
        if (library && !g) x = Tag(vg, x, y + 23, m.ic_not_lib, t.orange);
        if (a.archived) x = Tag(vg, x, y + 23, m.archived_tag, t.muted);
        bool newer = false;
        for (const auto& c : a.contents) newer = newer || NeedsNewer(a, c);
        if (newer) x = Tag(vg, x, y + 23, m.ic_dlc_needs_tag, t.orange);
        // the version, the DLC and where
        int dlc = 0;
        for (const auto& c : a.contents) dlc += c.type == NcmContentMetaType_AddOnContent;
        std::string sub;
        if (!a.archived) {
            char text[160];
            std::snprintf(text, sizeof(text), "v%u", GameVersion(a));
            sub = text;
            if (dlc) {
                std::snprintf(text, sizeof(text), m.ic_dlc_fmt, dlc);
                sub += std::string(" · ") + text;
            }
            sub += std::string(" · ") + WhereOf(a, s);
        } else {
            sub = m.ic_archived_sub;
        }
        ui::Text(vg, 134, y + 47, 14, t.muted, sub.c_str());
        nvgRestore(vg);
        if (!a.archived) ui::Text(vg, 1218, y + 33, 16, t.muted, draw::Size(a.Bytes()).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgGlobalAlpha(vg, 1.f);
    }
    // the names still being read, on a big list
    char left[96] = "";
    if (v.names_total > 0) std::snprintf(left, sizeof(left), m.ic_names_fmt, v.names_done, v.names_total);
    DrawListLetter(vg, kWidth / 2, 360);
    ui::HintBar(vg, t, left[0] ? left : nullptr,
                {{ui::glyph::A, m.ic_open, HidNpadButton_A},
                 {Glyphs(), s.view_switch, HidNpadButton_ZR},
                 {ui::glyph::B, s.back, HidNpadButton_B}},
                m_hint_rects);
}

void App::DrawConsolePage(NVGcontext* vg, const Space::View& v) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const InstalledApp* found = nullptr;
    for (const auto& a : v.apps) {
        if (a.app_id == m_console_app) found = &a;
    }
    if (!found) {
        // taken off whole: back to the list
        if (v.task == Space::Task::None) m_console_app = 0;
        return;
    }
    const InstalledApp& a = *found;
    const auto items = Items(a);
    if (m_console_marks.size() != items.size()) m_console_marks.assign(items.size(), 0);
    m_console_item = std::clamp(m_console_item, 0, std::max(0, int(items.size()) - 1));
    bool base_marked = false;
    for (size_t i = 0; i < items.size(); i++) {
        if (m_console_marks[i] && items[i].type == NcmContentMetaType_Application) base_marked = true;
    }

    const Game* g = LibraryGame(a.app_id);
    int image = g && !g->cover.empty() ? m_covers.Get(g->cover) : 0;
    if (!image) image = m_covers.Get(IconPath(a.app_id));
    ui::Cover(vg, t, 48, 100, 300, PlaceholderColor(a.name), Initial(a.name).c_str(), nullptr, false, false, 16, image);

    const float x0 = 384, w = kWidth - 48 - x0;
    nvgSave(vg);
    nvgScissor(vg, x0, 90, w, 60);
    ui::Text(vg, x0, 116, 26, t.text, a.name.c_str());
    nvgRestore(vg);
    char text[300];
    if (a.archived) {
        ui::Text(vg, x0, 150, 15, t.muted, m.ic_archived_sub);
        Paragraph(vg, x0, 200, w, 16, t.muted, m.ic_archived_note);
    } else {
        std::snprintf(text, sizeof(text), m.ic_meta_fmt, draw::Size(a.Bytes()).c_str(), WhereOf(a, s), GameVersion(a));
        ui::Text(vg, x0, 150, 15, t.muted, text);
    }

    const float top = 178, pitch = 56;
    const int visible = 7;
    const int first = std::clamp(m_console_item - visible + 1, 0, std::max(0, int(items.size()) - visible));
    unsigned long long freed = 0;
    int marked = 0;
    for (size_t i = 0; i < items.size(); i++) {
        if (m_console_marks[i] || base_marked) {
            freed += items[i].bytes;
            marked++;
        }
    }
    for (int r = first; r < int(items.size()) && r < first + visible; r++) {
        const auto& c = items[size_t(r)];
        const float y = top + (r - first) * pitch, cy = y + 24;
        if (r == m_console_item) {
            ui::FillRound(vg, x0, y, w, pitch - 6, 10, t.panel2);
            ui::Focus(vg, t, x0, y, w, pitch - 6, 10, m_time);
        } else {
            ui::FillRound(vg, x0, y, w, pitch - 6, 10, t.panel);
        }
        AddHit(x0, y, w, pitch - 6, HitKind::InstalledRow, r);
        const bool on = m_console_marks[size_t(r)] || base_marked;
        draw::TickBox(vg, t, x0 + 16, cy, on, "console:" + std::to_string(c.id) + ":" + std::to_string(c.version));
        nvgSave(vg);
        nvgScissor(vg, x0 + 52, y, w - 52 - 190, pitch);
        float x = ui::Text(vg, x0 + 52, cy, 17, t.text, ContentLabel(c, a.app_id).c_str()) + 12;
        if (NeedsNewer(a, c)) {
            std::snprintf(text, sizeof(text), m.ic_needs_fmt, c.required);
            x = Tag(vg, x, cy, text, t.orange);
        }
        nvgRestore(vg);
        std::snprintf(text, sizeof(text), "%s · %s", c.nand ? s.internal_short : "SD", draw::Size(c.bytes).c_str());
        ui::Text(vg, x0 + w - 16, cy, 15, t.muted, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    // what goes, how much it frees; the saves stay
    const float fy = top + visible * pitch + 14;
    if (base_marked) {
        ui::Text(vg, x0, fy, 15, t.muted, m.ic_with_game);
    }
    std::snprintf(text, sizeof(text), m.ic_marked_fmt, marked, draw::Size(freed).c_str());
    ui::Text(vg, x0, fy + (base_marked ? 24 : 0), 15, marked ? t.text : t.muted, text);

    std::vector<ui::Hint> hints;
    if (!items.empty()) {
        hints.push_back({ui::glyph::A, m.ic_mark, HidNpadButton_A});
        if (marked) hints.push_back({ui::glyph::X, m.ic_remove_marked, HidNpadButton_X});
    }
    if (g) hints.push_back({ui::glyph::Y, m.ic_from_library, HidNpadButton_Y});
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, v.task == Space::Task::Remove ? m.ic_removing : nullptr, hints, m_hint_rects);
}

void App::UpdateConsole(u64 down, u64 held) {
    if (!m_space) return;
    const auto v = m_space->Snapshot();
    if (!m_console_app) {
        if (v.apps.empty()) return;
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_console_focus > 0) m_console_focus--;
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_console_focus < int(v.apps.size()) - 1) m_console_focus++;
        // by first letter, for long lists
        const bool left = Repeat(down, held, HidNpadButton_AnyLeft), right = Repeat(down, held, HidNpadButton_AnyRight);
        if (left || right) {
            std::vector<std::string> names;
            for (const auto& a : v.apps) names.push_back(a.name);
            m_console_focus = ListJump(names, m_console_focus, right ? 1 : -1);
        }
        m_console_focus = std::clamp(m_console_focus, 0, int(v.apps.size()) - 1);
        if (down & HidNpadButton_A) {
            m_console_app = v.apps[size_t(m_console_focus)].app_id;
            m_console_item = 0;
            m_console_marks.clear();
        }
        return;
    }
    const InstalledApp* found = nullptr;
    for (const auto& a : v.apps) {
        if (a.app_id == m_console_app) found = &a;
    }
    if (down & HidNpadButton_B) {
        m_console_app = 0;
        return;
    }
    if (!found) return;
    const auto items = Items(*found);
    if (m_console_marks.size() != items.size()) m_console_marks.assign(items.size(), 0);
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_console_item > 0) m_console_item--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_console_item < int(items.size()) - 1) m_console_item++;
    m_console_item = std::clamp(m_console_item, 0, std::max(0, int(items.size()) - 1));
    if ((down & HidNpadButton_A) && !items.empty()) {
        m_console_marks[size_t(m_console_item)] = !m_console_marks[size_t(m_console_item)];
    }
    if (down & HidNpadButton_Y) {
        // the game in the library, to install what it lacks
        if (LibraryGame(m_console_app)) {
            for (int i = 0; i < int(m_view.size()); i++) {
                if (m_games[size_t(m_view[size_t(i)])].title_id == m_console_app) {
                    m_tab = Tab::Library;
                    m_focus = i;
                    OpenDetail(i);
                    return;
                }
            }
            Notice(TrM().ic_not_in_view);
        }
        return;
    }
    if (down & HidNpadButton_X) {
        bool base = false;
        for (size_t i = 0; i < items.size(); i++) {
            if (m_console_marks[i] && items[i].type == NcmContentMetaType_Application) base = true;
        }
        m_console_remove.clear();
        for (size_t i = 0; i < items.size(); i++) {
            if (m_console_marks[i] || base) m_console_remove.push_back(items[i]);
        }
        if (m_console_remove.empty()) {
            Notice(TrM().ic_nothing);
            return;
        }
        if (m_space->Busy() || (m_queue && (m_queue->Busy() || m_queue->Undoing()))) {
            Notice(TrM().cz_busy);
            return;
        }
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::ConsoleRemove;
        m_dialog.text = found->name;
    }
}

} // namespace nxdock
