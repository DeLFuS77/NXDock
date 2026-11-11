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
#include "i18n_tools.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/ns.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

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
    if (c.type == NcmContentMetaType_Application) return m.game_word;
    if (c.type == NcmContentMetaType_Patch) return VersionText(c.version);
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
    if (!m_versions) {
        // what titledb knows of updates and DLC, for the games not in the library (6A)
        m_versions = std::make_unique<VersionsList>();
        m_versions->Start(m_ui.versions_list);
    }
    const unsigned version = m_queue ? m_queue->Version() : 0;
    if (m_console_scanned_for != version && !m_space->Busy() && m_space->StartScan()) m_console_scanned_for = version;
}

void App::DrawConsole(NVGcontext* vg, const Space::View& all) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const Tools& tt = TrT();
    FollowSpace();
    FollowMissing(all);
    if (m_console_app) {
        DrawConsolePage(vg, all);
        return;
    }
    const Space::View v = ConsoleView(all);
    if (!v.scanned) {
        ui::Spinner(vg, kWidth / 2, 320, 14, t.work, m_time);
        ui::Text(vg, kWidth / 2, 366, 18, t.muted, m.ic_reading, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {{Glyphs(), s.view_switch, HidNpadButton_ZR}, {ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    if (all.apps.empty()) {
        ui::Text(vg, kWidth / 2, 340, 20, t.muted, m.ic_empty, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {{Glyphs(), s.view_switch, HidNpadButton_ZR}, {ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    const bool library = m_catalog.GetState() == Catalog::State::Ready;
    m_console_focus = std::clamp(m_console_focus, 0, int(v.apps.size()) - 1);
    // the size, at the right of the views: a tap chooses it
    {
        const char* names[] = {m.ic_size_list, m.ic_size_mid, m.ic_size_big};
        float x = kWidth - 48;
        for (int i = 2; i >= 0; i--) {
            const float w = ui::TextWidth(vg, 13, names[i]) + 22;
            x -= w;
            const bool on = m_ui.console_size == i;
            if (on) ui::FillRound(vg, x, 80, w, 24, 12, t.work);
            else ui::StrokeRound(vg, x, 80, w, 24, 12, t.line, 1.f);
            ui::Text(vg, x + w / 2, 92, 13, on ? t.on_work : t.muted, names[i], NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            AddHit(x - 2, 72, w + 4, 48, HitKind::ConsoleSize, i); // 48 px for a finger
            x -= 6;
        }
        // the filter: only the games missing an update or a DLC (6A)
        int missing = 0;
        for (const auto& a : all.apps) missing += !a.archived && MissingOf(a.app_id).Any();
        char text[96];
        std::snprintf(text, sizeof(text), tt.ms_filter_fmt, missing);
        const float w = ui::TextWidth(vg, 13, text) + 22;
        x -= 18 + w;
        if (m_console_missing) ui::FillRound(vg, x, 80, w, 24, 12, t.cyan);
        else ui::StrokeRound(vg, x, 80, w, 24, 12, t.line, 1.f);
        ui::Text(vg, x + w / 2, 92, 13, m_console_missing ? t.on_work : t.muted, text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        AddHit(x - 2, 72, w + 4, 48, HitKind::ConsoleMissing, 0);
    }
    if (v.apps.empty()) {
        // the filter on, and nothing missing
        ui::Text(vg, kWidth / 2, 340, 20, t.muted, tt.ms_nothing, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, ConsoleHints(), m_hint_rects);
        return;
    }
    if (m_ui.console_size != 0) {
        DrawConsoleGrid(vg, v, library);
        return;
    }
    const float top = 148, pitch = 72;
    const int visible = 7;
    ListArea(48, top, kWidth - 96, visible * pitch, pitch);
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
        const bool removing = m_console_removing.count(a.app_id) && v.task == Space::Task::Remove;
        nvgGlobalAlpha(vg, removing ? 0.25f : a.archived ? 0.55f : 1.f);
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
            sub = VersionText(GameVersion(a));
            if (dlc) {
                std::snprintf(text, sizeof(text), m.ic_dlc_fmt, dlc);
                sub += std::string(" · ") + text;
            }
            sub += std::string(" · ") + WhereOf(a, s);
        } else {
            sub = m.ic_archived_sub;
        }
        float px = ui::Text(vg, 134, y + 47, 14, t.muted, sub.c_str()) + 12;
        if (!a.archived) {
            // what is missing, in cyan and violet (6A)
            const Missing& ms = MissingOf(a.app_id);
            char pill[96];
            if (ms.update) px = Tag(vg, px, y + 47, VersionText(ms.update).c_str(), t.cyan);
            if (ms.dlc) {
                std::snprintf(pill, sizeof(pill), tt.ms_dlc_fmt, ms.dlc);
                px = Tag(vg, px, y + 47, pill, t.violet);
            }
        }
        nvgRestore(vg);
        const bool marking = !m_console_picked.empty();
        if (marking && !a.archived) draw::TickBox(vg, t, 1196, y + 33, m_console_picked.count(a.app_id) > 0, "cpick:" + std::to_string(a.app_id));
        if (!a.archived) {
            ui::Text(vg, marking ? 1180 : 1218, y + 33, 16, t.muted, draw::Size(a.Bytes()).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        nvgGlobalAlpha(vg, 1.f);
    }
    // the names still being read, on a big list
    char left[160] = "";
    PickedText(v, left, sizeof(left));
    DrawListLetter(vg, kWidth / 2, 360);
    ui::HintBar(vg, t, left[0] ? left : nullptr, ConsoleHints(), m_hint_rects);
}

// Medium (six in a row) or large covers (four in a row, with the version and the size).
void App::DrawConsoleGrid(NVGcontext* vg, const Space::View& v, bool library) {
    const Theme& t = m_theme;
    const More& m = TrM();
    const bool big = m_ui.console_size == 2;
    const int cols = big ? 4 : 6;
    const float cover = big ? 236 : 170, gap = big ? 34 : 32, below = big ? 58 : 36;
    const float x0 = (kWidth - (cols * cover + (cols - 1) * gap)) / 2, top = 132, bottom = 652;
    const float pitch = cover + below + 14;
    const int rows = (int(v.apps.size()) + cols - 1) / cols;
    const int row = m_console_focus / cols;
    // the row in focus whole in sight
    const int fit = std::max(1, int((bottom - top) / pitch));
    ListArea(0, top, kWidth, bottom - top, pitch);
    const int first = std::clamp(row - fit + 1, 0, std::max(0, rows - fit));
    nvgSave(vg);
    nvgScissor(vg, 0, top - 8, kWidth, bottom - top + 8);
    for (int i = first * cols; i < int(v.apps.size()); i++) {
        const int r = i / cols, c = i % cols;
        const float x = x0 + c * (cover + gap), y = top + (r - first) * pitch;
        if (y > bottom) break;
        const InstalledApp& a = v.apps[size_t(i)];
        AddHit(x, y, cover, cover + below, HitKind::InstalledRow, i);
        const Game* g = LibraryGame(a.app_id);
        int image = g && !g->cover.empty() ? m_covers.Get(g->cover) : 0;
        if (!image) image = m_covers.Get(IconPath(a.app_id));
        const bool removing = m_console_removing.count(a.app_id) && v.task == Space::Task::Remove;
        nvgGlobalAlpha(vg, removing ? 0.25f : a.archived ? 0.55f : 1.f);
        ui::Cover(vg, t, x, y, cover, PlaceholderColor(a.name), Initial(a.name).c_str(), nullptr, false, false, big ? 14 : 11, image);
        if (i == m_console_focus) ui::Focus(vg, t, x - 4, y - 4, cover + 8, cover + 8, big ? 17 : 14, m_time);
        if (!m_console_picked.empty() && !a.archived) {
            ui::FillRound(vg, x + cover - 38, y + 8, 30, 30, 8, nvgRGBAf(0, 0, 0, 0.55f));
            draw::TickBox(vg, t, x + cover - 34, y + 23, m_console_picked.count(a.app_id) > 0, "cpick:" + std::to_string(a.app_id));
        }
        // what to know at a glance, over the cover
        bool newer = false;
        for (const auto& ct : a.contents) newer = newer || NeedsNewer(a, ct);
        float by = y + 10;
        const auto badge = [&](const char* text, NVGcolor color) {
            const float w = std::min(cover - 16, ui::TextWidth(vg, 11, text) + 14);
            ui::FillRound(vg, x + 8, by, w, 18, 9, nvgRGBAf(0, 0, 0, 0.7f));
            ui::StrokeRound(vg, x + 8, by, w, 18, 9, color, 1.f);
            nvgSave(vg);
            nvgIntersectScissor(vg, x + 8, by, w, 18);
            ui::Text(vg, x + 8 + w / 2, by + 9, 11, color, text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgRestore(vg);
            by += 22;
        };
        if (library && !g) badge(m.ic_not_lib, t.orange);
        if (newer) badge(m.ic_dlc_needs_tag, t.orange);
        if (a.archived) badge(m.archived_tag, t.muted);
        if (!a.archived) {
            const Missing& ms = MissingOf(a.app_id);
            if (ms.update) badge(VersionText(ms.update).c_str(), t.cyan);
            if (ms.dlc) {
                char pill[96];
                std::snprintf(pill, sizeof(pill), TrT().ms_dlc_fmt, ms.dlc);
                badge(pill, t.violet);
            }
        }
        nvgSave(vg);
        nvgIntersectScissor(vg, x, y + cover, cover, below + 4);
        if (i == m_console_focus) {
            ui::Marquee(vg, x, y + cover + 18, cover, big ? 16 : 14, t.text, a.name.c_str(), m_time);
        } else {
            ui::Text(vg, x, y + cover + 18, big ? 16 : 14, t.text, a.name.c_str());
        }
        if (big && !a.archived) {
            int dlc = 0;
            for (const auto& ct : a.contents) dlc += ct.type == NcmContentMetaType_AddOnContent;
            char text[160];
            std::string sub = VersionText(GameVersion(a));
            if (dlc) {
                std::snprintf(text, sizeof(text), m.ic_dlc_fmt, dlc);
                sub += std::string(" · ") + text;
            }
            sub += std::string(" · ") + draw::Size(a.Bytes());
            ui::Text(vg, x, y + cover + 42, 13, t.muted, sub.c_str());
        }
        nvgRestore(vg);
        nvgGlobalAlpha(vg, 1.f);
    }
    nvgRestore(vg);
    char left[160] = "";
    PickedText(v, left, sizeof(left));
    ui::HintBar(vg, t, left[0] ? left : nullptr, ConsoleHints(), m_hint_rects);
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
    const Tools& tt = TrT();
    const Missing& ms = MissingOf(a.app_id);
    if (!a.archived) {
        // what it lacks, and where it could come from (6A)
        const float cx = 48, cw = 300;
        float cy0 = 418;
        draw::Panel(vg, t, cx, cy0, cw, 216, 12);
        ui::Text(vg, cx + 16, cy0 + 24, 16, ms.Any() ? t.text : t.muted, ms.Any() ? tt.ms_card : tt.ms_nothing);
        cy0 += 52;
        char line[200];
        if (ms.update) {
            std::snprintf(line, sizeof(line), tt.ms_update_fmt, VersionText(ms.update).c_str(), VersionText(GameVersion(a)).c_str());
            ui::Text(vg, cx + 16, cy0, 14, t.cyan, line);
            ui::Text(vg, cx + 16, cy0 + 20, 13, ms.update_lib ? t.ok : t.muted, ms.update_lib ? tt.ms_in_lib : tt.ms_not_lib);
            cy0 += 48;
        }
        if (ms.dlc) {
            std::snprintf(line, sizeof(line), tt.ms_dlc_of_fmt, ms.dlc, ms.dlc_known);
            ui::Text(vg, cx + 16, cy0, 14, t.violet, (std::string(tt.ms_dlc) + " · " + line).c_str());
            if (ms.dlc_lib) std::snprintf(line, sizeof(line), tt.ms_some_lib_fmt, ms.dlc_lib);
            ui::Text(vg, cx + 16, cy0 + 20, 13, ms.dlc_lib ? t.ok : t.muted, ms.dlc_lib ? line : tt.ms_not_lib);
            cy0 += 48;
        }
        // where the versions come from
        std::string list = tt.ms_list_none;
        if (m_versions && m_versions->Made() > 0 && m_ui.versions_list) {
            const std::time_t when = std::time_t(m_versions->Made());
            std::tm tm{};
            localtime_r(&when, &tm);
            char date[32];
            std::strftime(date, sizeof(date), "%d/%m %H:%M", &tm);
            std::snprintf(line, sizeof(line), tt.ms_list_fmt, date);
            list = line;
        }
        ui::Text(vg, cx + 16, 418 + 216 - 18, 12, t.muted, (std::string(tt.ms_list) + " · " + list).c_str());
    }

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
        std::snprintf(text, sizeof(text), m.ic_meta_fmt, draw::Size(a.Bytes()).c_str(), WhereOf(a, s), VersionText(GameVersion(a)).c_str());
        ui::Text(vg, x0, 150, 15, t.muted, text);
    }

    const float top = 178, pitch = 56;
    const int visible = 7;
    ListArea(x0, top, w, visible * pitch, pitch);
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
            std::snprintf(text, sizeof(text), m.ic_needs_fmt, VersionText(c.required).c_str());
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

    // as everywhere: Y marks, − deletes what is marked
    std::vector<ui::Hint> hints;
    if (!items.empty()) {
        hints.push_back({ui::glyph::A, tt.cd_hint, HidNpadButton_A});
        hints.push_back({ui::glyph::Y, m.ic_mark, HidNpadButton_Y});
        if (marked) hints.push_back({ui::glyph::Minus, m.ic_remove_marked, HidNpadButton_Minus});
    }
    if (g) hints.push_back({ui::glyph::X, ms.Any() && (ms.update_lib || ms.dlc_lib) ? tt.ms_install_hint : m.ic_from_library, HidNpadButton_X});
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, v.task == Space::Task::Remove ? m.ic_removing : nullptr, hints, m_hint_rects);
}

// How many are marked and what they take, or the names still being read; on the left of the hints.
void App::PickedText(const Space::View& v, char* out, size_t size) {
    const More& m = TrM();
    out[0] = 0;
    if (!m_console_picked.empty()) {
        unsigned long long bytes = 0;
        int n = 0;
        for (const auto& a : v.apps) {
            if (m_console_picked.count(a.app_id)) {
                bytes += a.Bytes();
                n++;
            }
        }
        std::snprintf(out, size, m.ic_picked_fmt, n, draw::Size(bytes).c_str());
    } else if (v.names_total > 0) {
        std::snprintf(out, size, m.ic_names_fmt, v.names_done, v.names_total);
    }
}

std::vector<ui::Hint> App::ConsoleHints() {
    const auto& s = Tr();
    const More& m = TrM();
    static std::string many;
    // as everywhere: A opens, Y marks, − deletes what is marked
    std::vector<ui::Hint> hints = {{ui::glyph::A, m.ic_open, HidNpadButton_A}, {ui::glyph::Y, m.ic_mark, HidNpadButton_Y}};
    if (!m_console_picked.empty()) {
        char text[120];
        std::snprintf(text, sizeof(text), m.ic_rm_picked_fmt, int(m_console_picked.size()));
        many = text;
        hints.push_back({ui::glyph::Minus, many.c_str(), HidNpadButton_Minus});
    } else {
        hints.push_back({ui::glyph::Minus, m_console_missing ? TrT().ms_hint_all : TrT().ms_hint_filter, HidNpadButton_Minus});
    }
    hints.push_back({ui::glyph::X, m.ic_size, HidNpadButton_X});
    hints.push_back({Glyphs(), s.view_switch, HidNpadButton_ZR});
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    return hints;
}

void App::AskConsoleRemoveMany(const Space::View& v) {
    m_dialog = Dialog{};
    for (const auto& a : v.apps) {
        if (m_console_picked.count(a.app_id) && !a.contents.empty()) m_dialog.ids.push_back(a.app_id);
    }
    if (m_dialog.ids.empty()) {
        Notice(TrM().ic_nothing);
        return;
    }
    if (m_space->Busy() || (m_queue && (m_queue->Busy() || m_queue->Undoing()))) {
        m_dialog = Dialog{};
        Notice(TrM().cz_busy);
        return;
    }
    m_dialog.kind = DialogKind::ConsoleRemove;
}

void App::UpdateConsole(u64 down, u64 held) {
    if (!m_space) return;
    const auto all = m_space->Snapshot();
    if (!m_console_app) {
        if ((down & HidNpadButton_Minus) && m_console_picked.empty()) {
            // the filter of what is missing (6A)
            m_console_missing = !m_console_missing;
            m_console_focus = 0;
            return;
        }
        const auto v = ConsoleView(all);
        if (down & HidNpadButton_X) {
            // the size: list, medium covers, large covers; kept for the next time
            m_ui.console_size = (m_ui.console_size + 1) % 3;
            m_ui.Save();
            return;
        }
        if (v.apps.empty()) return;
        const int n = int(v.apps.size());
        m_console_focus = std::clamp(m_console_focus, 0, n - 1);
        if (down & HidNpadButton_Y) {
            // marked to take off together; an archived game has nothing to take
            const auto& a = v.apps[size_t(m_console_focus)];
            if (!a.archived) {
                if (!m_console_picked.erase(a.app_id)) m_console_picked.insert(a.app_id);
            }
        }
        if ((down & HidNpadButton_Minus) && !m_console_picked.empty()) {
            AskConsoleRemoveMany(v);
            return;
        }
        if (m_ui.console_size != 0) {
            const int cols = m_ui.console_size == 2 ? 4 : 6;
            if (Repeat(down, held, HidNpadButton_AnyLeft) && m_console_focus > 0) m_console_focus--;
            if (Repeat(down, held, HidNpadButton_AnyRight) && m_console_focus < n - 1) m_console_focus++;
            if (Repeat(down, held, HidNpadButton_AnyUp) && m_console_focus >= cols) m_console_focus -= cols;
            if (Repeat(down, held, HidNpadButton_AnyDown) && m_console_focus / cols < (n - 1) / cols) {
                m_console_focus = std::min(n - 1, m_console_focus + cols);
            }
        } else {
            if (Repeat(down, held, HidNpadButton_AnyUp) && m_console_focus > 0) m_console_focus--;
            if (Repeat(down, held, HidNpadButton_AnyDown) && m_console_focus < n - 1) m_console_focus++;
        }
        // by first letter, for long lists
        const bool left = m_ui.console_size == 0 && Repeat(down, held, HidNpadButton_AnyLeft),
                   right = m_ui.console_size == 0 && Repeat(down, held, HidNpadButton_AnyRight);
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
    const auto& v = all;
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
    if ((down & HidNpadButton_Y) && !items.empty()) {
        m_console_marks[size_t(m_console_item)] = !m_console_marks[size_t(m_console_item)];
    }
    if ((down & HidNpadButton_A) && !items.empty()) {
        // the details of the line: its ticket and the version the console asks for (7A)
        OpenContentDetails(*found, items[size_t(m_console_item)]);
        return;
    }
    if (down & HidNpadButton_X) {
        // the game in the library, to install what it lacks; B comes back here
        if (LibraryGame(m_console_app)) {
            for (int i = 0; i < int(m_view.size()); i++) {
                if (m_games[size_t(m_view[size_t(i)])].title_id == m_console_app) {
                    m_tab = Tab::Library;
                    m_focus = i;
                    OpenDetail(i);
                    m_detail_from_console = true;
                    return;
                }
            }
            Notice(TrM().ic_not_in_view);
        }
        return;
    }
    if (down & HidNpadButton_Minus) {
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

// ---- what is missing (6A) ----

const App::Missing& App::MissingOf(unsigned long long app) {
    static const Missing none;
    const auto it = m_missing.find(app);
    return it == m_missing.end() ? none : it->second;
}

void App::FollowMissing(const Space::View& v) {
    if (!v.scanned) return;
    const long long made = m_versions && m_ui.versions_list ? m_versions->Made() : 0;
    const std::string key = std::to_string(v.apps.size()) + ":" + std::to_string(m_games_version) + ":" + std::to_string(made) + ":" +
                            std::to_string(m_console_scanned_for) + ":" + std::to_string(m_games.size());
    if (key == m_missing_key) return;
    m_missing_key = key;
    m_missing.clear();
    const bool list = m_versions && m_ui.versions_list && m_versions->Ready();
    for (const auto& a : v.apps) {
        if (a.archived) continue;
        Missing ms;
        const unsigned have = GameVersion(a);
        std::set<unsigned long long> have_dlc;
        for (const auto& c : a.contents) {
            if (c.type == NcmContentMetaType_AddOnContent) have_dlc.insert(c.id);
        }
        unsigned lib_update = 0;
        std::set<unsigned long long> lib_dlc, known;
        if (const Game* g = LibraryGame(a.app_id)) {
            for (const auto& f : g->files) {
                if (f.kind == CatalogFile::Kind::Update) lib_update = std::max(lib_update, unsigned(f.version));
                if (f.kind == CatalogFile::Kind::Dlc) lib_dlc.insert(f.title_id);
            }
        }
        known = lib_dlc;
        unsigned newest = lib_update;
        if (list) {
            newest = std::max(newest, unsigned(m_versions->LatestUpdate(a.app_id)));
            for (const auto id : m_versions->Dlcs(a.app_id)) known.insert(id);
        }
        if (newest > have) {
            ms.update = newest;
            ms.update_lib = lib_update > have;
        }
        ms.dlc_known = int(known.size());
        for (const auto id : known) {
            if (have_dlc.count(id)) continue;
            ms.dlc++;
            if (lib_dlc.count(id)) ms.dlc_lib++;
        }
        if (ms.Any()) m_missing[a.app_id] = ms;
    }
}

Space::View App::ConsoleView(const Space::View& v) {
    if (!m_console_missing) return v;
    FollowMissing(v);
    Space::View out = v;
    out.apps.clear();
    for (const auto& a : v.apps) {
        if (!a.archived && MissingOf(a.app_id).Any()) out.apps.push_back(a);
    }
    return out;
}

// ---- the details of a content (7A) ----

namespace {

std::string FwText(u32 v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%u.%u.%u", (v >> 26) & 0x3F, (v >> 20) & 0x3F, (v >> 16) & 0xF);
    return text;
}

} // namespace

void App::OpenContentDetails(const InstalledApp& a, const InstalledApp::Content& c) {
    const Tools& tt = TrT();
    const auto& s = Tr();
    m_tool = ToolDialog{};
    char text[300];
    std::snprintf(text, sizeof(text), tt.cd_title_fmt, ContentLabel(c, a.app_id).c_str());
    m_tool.title = text;
    std::snprintf(text, sizeof(text), "%016llX", c.id);
    m_tool.lines.push_back({tt.cd_id, text});
    std::snprintf(text, sizeof(text), "v%u", c.version);
    m_tool.lines.push_back({tt.cd_version, c.type == NcmContentMetaType_Patch ? VersionText(c.version) + " (" + text + ")" : std::string(text)});
    m_tool.lines.push_back({tt.cd_where, std::string(c.nand ? s.internal : s.sd_card) + " · " + draw::Size(c.bytes)});

    // what the console has of it: its rights, the firmware it asks for
    const NcmStorageId storage = c.nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;
    NcmContentMetaDatabase db{};
    NcmContentStorage cs{};
    bool ticket_missing = false;
    if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, storage))) {
        if (R_SUCCEEDED(ncmOpenContentStorage(&cs, storage))) {
            NcmContentMetaKey key{};
            key.id = c.id;
            key.version = c.version;
            key.type = c.type;
            key.install_type = NcmContentInstallType_Full;
            NcmContentId cid{};
            const NcmContentType kind = c.type == NcmContentMetaType_AddOnContent ? NcmContentType_Data : NcmContentType_Program;
            NcmRightsId rid{};
            if (R_SUCCEEDED(ncmContentMetaDatabaseGetContentIdByType(&db, &cid, &key, kind)) &&
                R_SUCCEEDED(ncmContentStorageGetRightsIdFromContentId(&cs, &rid, &cid, FsContentAttributes_All))) {
                std::string ticket = tt.cd_t_none;
                if (sphaira::es::IsRightsIdValid(rid.rights_id)) {
                    bool common = false, personal = false;
                    if (R_SUCCEEDED(sphaira::es::Initialize())) {
                        sphaira::es::IsRightsIdCommon(rid.rights_id, &common);
                        sphaira::es::IsRightsIdPersonalised(rid.rights_id, &personal);
                    }
                    ticket = common ? tt.cd_t_common : personal ? tt.cd_t_personal : tt.cd_t_missing;
                    ticket_missing = !common && !personal;
                }
                m_tool.lines.push_back({tt.cd_ticket, ticket, ticket_missing});
                const int gen = rid.key_generation ? int(rid.key_generation) : -1;
                if (gen >= 0) {
                    if (m_sysinfo.key_generation >= 0) {
                        std::snprintf(text, sizeof(text), tt.cd_keys_fmt, gen, m_sysinfo.key_generation);
                    } else {
                        std::snprintf(text, sizeof(text), tt.cd_keys_one_fmt, gen);
                    }
                    m_tool.lines.push_back({tt.cd_keys, text, m_sysinfo.key_generation >= 0 && gen > m_sysinfo.key_generation});
                }
            }
            u32 required = 0;
            if (c.type == NcmContentMetaType_AddOnContent) {
                if (R_SUCCEEDED(ncmContentMetaDatabaseGetRequiredApplicationVersion(&db, &required, &key)) && required) {
                    m_tool.lines.push_back({tt.cd_needs_app, VersionText(required), required > GameVersion(a)});
                }
            } else if (R_SUCCEEDED(ncmContentMetaDatabaseGetRequiredSystemVersion(&db, &required, &key)) && required) {
                std::string fw = FwText(required);
                if (m_sysinfo.firmware) {
                    std::snprintf(text, sizeof(text), tt.cd_firmware_fmt, FwText(required).c_str(), FwText(m_sysinfo.firmware).c_str());
                    fw = text;
                }
                m_tool.lines.push_back({tt.cd_firmware, fw, m_sysinfo.firmware && required > m_sysinfo.firmware});
            }
            ncmContentStorageClose(&cs);
        }
        ncmContentMetaDatabaseClose(&db);
    }
    // the version the console asks for before it opens the game
    bool asks_more = false;
    if (R_SUCCEEDED(avmInitialize())) {
        u32 asks = 0;
        if (R_SUCCEEDED(avmGetLaunchRequiredVersion(a.app_id, &asks)) && asks > GameVersion(a)) {
            std::snprintf(text, sizeof(text), tt.cd_asks_fmt, VersionText(asks).c_str());
            m_tool.lines.push_back({tt.cd_asks, text, true});
            asks_more = true;
        }
        avmExit();
    }
    if (ticket_missing) m_tool.note = tt.cd_missing_note;
    m_tool.primary = tt.cd_version_go;
    m_tool.can = asks_more;
    m_tool.cancel = s.close;
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::ContentDetails;
    m_dialog.ids = {a.app_id, c.id, c.version, c.type, c.nand ? 1ull : 0ull};
    m_dialog.text = a.name;
}

void App::ConsoleTicketBack() {
    if (m_dialog.ids.size() < 5) return;
    const unsigned long long app = m_dialog.ids[0], id = m_dialog.ids[1];
    const u32 version = u32(m_dialog.ids[2]);
    const bool nand = m_dialog.ids[4] != 0;
    const Game* g = LibraryGame(app);
    const CatalogFile* file = nullptr;
    if (g) {
        for (const auto& f : g->files) {
            if (f.title_id == id && f.version == version) file = &f;
        }
    }
    m_dialog = Dialog{};
    if (!g || !file) {
        Notice(TrT().cd_no_lib);
        return;
    }
    // its parts are on the console and stay; only its ticket (and its record) is written again
    InstallJob job = JobFor(*g, *file);
    job.ticket_only = true;
    job.reinstall_ok = true;
    job.nand = nand;
    job.label += std::string(" · ") + TrT().cd_ticket_label;
    AddJobs({job});
}

void App::ConsoleVersionReset() {
    if (m_dialog.ids.empty()) return;
    const unsigned long long app = m_dialog.ids[0];
    // the version of what is installed, as the console reads it from its records
    if (R_SUCCEEDED(sphaira::ns::Initialize()) && R_SUCCEEDED(sphaira::ns::SetLowestLaunchVersion(app))) {
        NoticeOk(TrT().cd_version_done);
    } else {
        Notice(TrT().cd_version_fail);
    }
}

} // namespace nxdock
