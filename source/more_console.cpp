// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// In the console (1A and 2A of the visual test of 11/11): every game installed on the console, also
// those not in the library (installed with DBI, from the eShop or a game card), each with its icon
// of the console; and the page of a game with what it has (the game, its update, each DLC, told
// apart), where what goes is marked and taken off after a question. The saves are never touched.
#include "shortcut_filter.hpp"
#include "app.hpp"

#include "console_order.hpp"
#include "draw.hpp"
#include "fileops.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"
#include "log.hpp"
#include "mods.hpp"
#include "net.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/ns.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <random>
#include <sys/stat.h>

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

// A small clock, before the play time (3A).
void Clock(NVGcontext* vg, float cx, float cy, float r, NVGcolor c) {
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, 1.5f);
    nvgLineCap(vg, NVG_ROUND);
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx, cy - r + 2.5f);
    nvgLineTo(vg, cx, cy);
    nvgLineTo(vg, cx + r - 2.5f, cy);
    nvgStroke(vg);
}

const char* SortName(const More& m, int sort) {
    switch (sort) {
        case kSortSize: return m.so_size;
        case kSortLast: return m.so_last;
        case kSortTime: return m.so_time;
        case kSortWhere: return m.so_where;
        default: return m.so_name;
    }
}

bool NeverOpened(const InstalledApp& a) {
    return a.launches == 0 || (a.play_minutes <= 0 && a.idle_minutes < 0);
}

// How long a game was played: "42 h 10 min", or that it was never opened.
std::string PlayedText(const InstalledApp& a) {
    const More& m = TrM();
    if (NeverOpened(a)) return m.ps_never;
    return PlayTimeText(a.play_minutes, m.ps_h_min_fmt, m.ps_h_fmt, m.ps_min_fmt);
}

// The line of the page of a game: "42 h 10 min · opened 37 times · last played 3 days ago".
std::string StatsText(const InstalledApp& a) {
    const More& m = TrM();
    if (NeverOpened(a)) return m.ps_never;
    const std::string ago = a.idle_minutes < 0 ? std::string("?")
                                               : AgoText(a.idle_minutes, m.ps_today, m.ps_yesterday, m.ps_days_fmt, m.ps_weeks_fmt, m.ps_months_fmt);
    char text[200];
    if (a.launches == 1) std::snprintf(text, sizeof(text), m.ps_stats_once_fmt, PlayedText(a).c_str(), ago.c_str());
    else std::snprintf(text, sizeof(text), m.ps_stats_fmt, PlayedText(a).c_str(), std::max(0, a.launches), ago.c_str());
    return text;
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

void App::DrawConsole(NVGcontext* vg, const Space::View& raw) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const Tools& tt = TrT();
    FollowSpace();
    FollowMissing(raw);
    if (m_console_app) {
        DrawConsolePage(vg, raw);
        return;
    }
    const Space::View v = ConsoleView(raw);
    const Space::View all = ConsoleApps(raw); // the counts of the filters, of what is listed
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
    m_console_focus = std::clamp(m_console_focus, 0, std::max(0, int(v.apps.size()) - 1));
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
        int missing = 0, orphans = 0;
        for (const auto& a : all.apps) {
            missing += !a.archived && !WithoutGame(a) && MissingOf(a.app_id).Any();
            orphans += WithoutGame(a);
        }
        char text[96];
        std::snprintf(text, sizeof(text), tt.ms_filter_fmt, missing);
        const float w = ui::TextWidth(vg, 13, text) + 22;
        x -= 18 + w;
        if (m_console_missing) ui::FillRound(vg, x, 80, w, 24, 12, t.cyan);
        else ui::StrokeRound(vg, x, 80, w, 24, 12, t.line, 1.f);
        ui::Text(vg, x + w / 2, 92, 13, m_console_missing ? t.on_work : t.muted, text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        AddHit(x - 2, 72, w + 4, 48, HitKind::ConsoleMissing, 0);
        // the updates and DLC without their game, in orange (5C); only when there are some
        if (orphans > 0 || m_console_orphans) {
            std::snprintf(text, sizeof(text), m.og_chip_fmt, orphans);
            const float ow = ui::TextWidth(vg, 13, text) + 22;
            x -= 8 + ow;
            if (m_console_orphans) ui::FillRound(vg, x, 80, ow, 24, 12, t.orange);
            else ui::StrokeRound(vg, x, 80, ow, 24, 12, t.orange, 1.2f);
            ui::Text(vg, x + ow / 2, 92, 13, m_console_orphans ? t.on_work : t.orange, text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            AddHit(x - 2, 72, ow + 4, 48, HitKind::ConsoleOrphans, 0);
        }
        // the order chosen in + (4A), when there is room left of the filters
        std::snprintf(text, sizeof(text), m.so_head_fmt, SortName(m, m_ui.console_sort));
        if (x - 18 - ui::TextWidth(vg, 14, text) > 470) {
            const float k = ui::EaseOut(ui::Appear(m_console_list_at, 0.3f));
            nvgGlobalAlpha(vg, k);
            ui::Text(vg, x - 18, 92 - (1 - k) * 8, 14, t.muted, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgGlobalAlpha(vg, 1.f);
        }
    }
    if (v.apps.empty()) {
        // a filter on, and nothing for it
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
        // the rows come in one after another, after a new order or a filter
        const float in = ui::EaseOut(ui::Appear(m_console_list_at + 0.04 * (r - first), 0.3f));
        nvgSave(vg);
        nvgTranslate(vg, -(1 - in) * 24, 0);
        if (r == m_console_focus) {
            nvgGlobalAlpha(vg, in);
            ui::FillRound(vg, 48, y, kWidth - 96, pitch - 6, 12, t.panel2);
            ui::Focus(vg, t, 48, y, kWidth - 96, pitch - 6, 12, m_time);
        }
        AddHit(48, y, kWidth - 96, pitch - 6, HitKind::InstalledRow, r);
        const Game* g = LibraryGame(a.app_id);
        int image = g && !g->cover.empty() ? m_covers.Get(g->cover) : 0;
        if (!image) image = m_covers.Get(IconPath(a.app_id));
        const bool removing = m_console_removing.count(a.app_id) && v.task == Space::Task::Remove;
        const bool orphan = WithoutGame(a);
        // an update or a DLC without its game, lighter (5C)
        nvgGlobalAlpha(vg, in * (removing ? 0.25f : a.archived ? 0.55f : orphan ? 0.62f : 1.f));
        ui::Cover(vg, t, 62, y + 5, 56, PlaceholderColor(a.name), Initial(a.name).c_str(), nullptr, false, false, 9, image);
        nvgSave(vg);
        nvgScissor(vg, 134, y, 920, pitch);
        float x = ui::Text(vg, 134, y + 23, 18, t.text, a.name.c_str()) + 12;
        if (orphan) x = Tag(vg, x, y + 23, m.og_tag, t.orange);
        if (library && !g) x = Tag(vg, x, y + 23, m.ic_not_lib, t.orange);
        if (a.archived) x = Tag(vg, x, y + 23, m.archived_tag, t.muted);
        bool newer = false;
        for (const auto& c : a.contents) newer = newer || NeedsNewer(a, c);
        if (newer) x = Tag(vg, x, y + 23, m.ic_dlc_needs_tag, t.orange);
        // the version, the DLC and where
        int dlc = 0;
        bool update = false;
        for (const auto& c : a.contents) {
            dlc += c.type == NcmContentMetaType_AddOnContent;
            update = update || c.type == NcmContentMetaType_Patch;
        }
        std::string sub;
        if (!a.archived) {
            char text[160];
            if (!orphan || update) sub = VersionText(GameVersion(a));
            if (dlc) {
                std::snprintf(text, sizeof(text), m.ic_dlc_fmt, dlc);
                sub += std::string(sub.empty() ? "" : " · ") + text;
            }
            sub += std::string(" · ") + WhereOf(a, s);
            if (orphan) sub += std::string(" · ") + m.og_sub;
        } else {
            sub = m.ic_archived_sub;
        }
        float px = ui::Text(vg, 134, y + 47, 14, t.muted, sub.c_str()) + 12;
        if (!a.archived && !orphan && (a.play_minutes >= 0 || a.launches >= 0)) {
            // how long it was played, with a small clock (3A)
            px = ui::Text(vg, px - 4, y + 47, 14, t.muted, "· ") + 2;
            Clock(vg, px + 6, y + 47, 6, t.muted);
            px = ui::Text(vg, px + 16, y + 47, 14, t.muted, PlayedText(a).c_str()) + 12;
        }
        if (!a.archived && !orphan) {
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
        nvgRestore(vg);
    }
    // the names still being read, on a big list
    char left[160] = "";
    PickedText(v, left, sizeof(left));
    DrawListLetter(vg, kWidth / 2, 360);
    DrawRemoveProgress(vg, v);
    ui::HintBar(vg, t, left[0] ? left : nullptr, ConsoleHints(), m_hint_rects);
}

// Cancel while several games are taken off: it stops before the next game (never inside one).
void App::StopRemove() {
    if (!m_space || m_console_rm_stop) return;
    m_console_rm_stop = true;
    m_space->Cancel();
}

// While games are taken off: which one, of how many, copying its saves or deleting it, over the
// hint bar, where it is seen.
void App::DrawRemoveProgress(NVGcontext* vg, const Space::View& v) {
    if (v.task != Space::Task::Remove || v.remove_count <= 0) return;
    const Theme& t = m_theme;
    const More& m = TrM();
    std::string name;
    for (const auto& a : v.apps) {
        if (a.app_id == v.remove_app) name = a.name;
    }
    char text[300];
    if (m_console_rm_stop) std::snprintf(text, sizeof(text), m.ic_rm_stopping_fmt, name.c_str());
    else if (v.remove_saving) std::snprintf(text, sizeof(text), m.ic_rm_saving_fmt, name.c_str());
    else std::snprintf(text, sizeof(text), m.ic_rm_progress_fmt, name.c_str(), v.remove_index, v.remove_count);
    const float w = 760, h = 70, x = (kWidth - w) / 2, y = kHeight - 56 - h - 14;
    const float in = ui::EaseOut(ui::Appear(m_console_list_at, 0.3f));
    nvgSave(vg);
    nvgGlobalAlpha(vg, in);
    draw::Popup(vg, t, x, y, w, h, 16);
    ui::Spinner(vg, x + 32, y + 26, 9, t.work, m_time);
    // Cancel, several games only: B, or a tap anywhere on its side of the card
    float bw = 0;
    if (v.remove_count > 1 && !m_console_rm_stop) {
        const auto& s = Tr();
        bw = ui::TextWidth(vg, 16, s.cancel) + 64;
        const float bx = x + w - bw - 12, by = y + 9, bh = 34;
        ui::FillRound(vg, bx, by, bw, bh, 10, t.panel2);
        ui::StrokeRound(vg, bx + 1, by + 1, bw - 2, bh - 2, 10, t.line, 2);
        ui::Symbol(vg, t, bx + 22, by + bh / 2, 20, ui::glyph::B, t.text, HidNpadButton_B);
        ui::Text(vg, bx + 38, by + bh / 2, 16, t.text, s.cancel);
        AddHit(bx - 12, y, bw + 24, h, HitKind::RemoveStop);
        bw += 24;
    }
    nvgSave(vg);
    nvgIntersectScissor(vg, x + 52, y + 8, w - 72 - bw, 36);
    ui::Text(vg, x + 52, y + 26, 17, t.text, text);
    nvgRestore(vg);
    const float frac = v.total ? float(double(v.done) / double(v.total)) : 0.f;
    ui::Progress(vg, t, x + 20, y + 50, w - 40, 8, frac, t.work, m_time, true);
    nvgRestore(vg);
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
        const bool orphan = WithoutGame(a);
        nvgGlobalAlpha(vg, removing ? 0.25f : a.archived ? 0.55f : orphan ? 0.62f : 1.f);
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
        if (orphan) badge(m.og_tag, t.orange);
        if (library && !g) badge(m.ic_not_lib, t.orange);
        if (newer) badge(m.ic_dlc_needs_tag, t.orange);
        if (a.archived) badge(m.archived_tag, t.muted);
        if (!a.archived && !orphan) {
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
    DrawRemoveProgress(vg, v);
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
    const bool orphan = WithoutGame(a);
    const bool play = ConsoleCanPlay(a);
    if (!play && m_console_btn == 1) m_console_btn = 0;

    const Game* g = LibraryGame(a.app_id);
    int image = g && !g->cover.empty() ? m_covers.Get(g->cover) : 0;
    if (!image) image = m_covers.Get(IconPath(a.app_id));
    nvgGlobalAlpha(vg, orphan ? 0.62f : 1.f);
    ui::Cover(vg, t, 48, 100, 300, PlaceholderColor(a.name), Initial(a.name).c_str(), nullptr, false, false, 16, image);
    nvgGlobalAlpha(vg, 1.f);
    const Tools& tt = TrT();
    const Missing& ms = MissingOf(a.app_id);
    if (orphan) {
        // an update or a DLC without its game (5C): what that means, and whether the library has it
        const float cx = 48, cw = 300, cy0 = 418;
        draw::Panel(vg, t, cx, cy0, cw, 216, 12);
        ui::Text(vg, cx + 16, cy0 + 24, 16, t.text, m.og_not_here);
        Paragraph(vg, cx + 16, cy0 + 50, cw - 32, 14, t.orange, m.og_cannot);
        const bool has = LibraryBase(a.app_id);
        ui::Text(vg, cx + 16, cy0 + 120, 13, has ? t.ok : t.muted, has ? m.og_in_lib : m.og_not_lib);
    } else if (!a.archived) {
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
    char text[300];
    // the title ID, on the right of the name (4A)
    std::snprintf(text, sizeof(text), "ID %016llX", a.app_id);
    const float idw = ui::TextWidth(vg, 15, text);
    ui::Text(vg, x0 + w, 116, 15, t.muted, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgSave(vg);
    nvgScissor(vg, x0, 90, w - idw - 20, 60);
    float nx = ui::Text(vg, x0, 116, 26, t.text, a.name.c_str()) + 14;
    if (orphan) Tag(vg, nx, 118, m.og_tag, t.orange);
    nvgRestore(vg);
    float hy = 150;
    if (a.archived) {
        ui::Text(vg, x0, hy, 15, t.muted, m.ic_archived_sub);
        Paragraph(vg, x0, 200, w, 16, t.muted, m.ic_archived_note);
    } else {
        std::snprintf(text, sizeof(text), m.ic_meta_fmt, draw::Size(a.Bytes()).c_str(), WhereOf(a, s),
                      orphan ? m.og_sub : VersionText(GameVersion(a)).c_str());
        ui::Text(vg, x0, hy, 15, t.muted, text);
    }
    if (!a.archived && !orphan && (a.play_minutes >= 0 || a.launches >= 0)) {
        // how long it was played, how many times it was opened and when last (3A)
        hy += 26;
        Clock(vg, x0 + 7, hy, 7, t.work);
        ui::Text(vg, x0 + 22, hy, 15, t.text, StatsText(a).c_str());
    }
    float top = hy + 28;
    if (play) {
        // Play, big and blue: what is done most from here (2A)
        const float by = hy + 20, bh = 56, bw = std::max(200.f, ui::TextWidth(vg, 18, m.pl_play) + 80);
        if (m_console_btn == 1) ui::Focus(vg, t, x0, by, bw, bh, 14, m_time);
        ui::FillRound(vg, x0, by, bw, bh, 14, t.work);
        const float icx = x0 + 30, icy = by + bh / 2;
        nvgBeginPath(vg);
        nvgMoveTo(vg, icx - 6, icy - 9);
        nvgLineTo(vg, icx + 9, icy);
        nvgLineTo(vg, icx - 6, icy + 9);
        nvgClosePath(vg);
        nvgFillColor(vg, t.on_work);
        nvgFill(vg);
        ui::Text(vg, x0 + 52, icy, 18, t.on_work, m.pl_play);
        AddHit(x0, by, bw, bh, HitKind::ConsoleButton, 1);
        top = by + bh + 18;
    }

    // the check of this game, when one was asked for from its page (1A)
    const bool checked = m_console_check == a.app_id && m_queue;
    const auto cv = checked ? m_queue->GetVerify() : InstallQueue::VerifyView{};
    if (checked && cv.done && IsCancelled(cv.rc)) m_console_check = 0; // stopped: as before
    const float pitch = 56, panel_h = 84;
    const int visible = std::max(1, int((650 - top - (checked ? panel_h + 8 : 40)) / pitch));
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
    std::string bad_names;
    int parts = 0, bad_count = 0;
    if (checked && cv.done) {
        for (const auto& c : cv.result) {
            parts++;
            if (c.bad.empty()) continue;
            bad_count++;
            InstalledApp::Content ct{c.key.id, c.key.version, c.key.type, c.nand, 0, 0};
            bad_names += (bad_names.empty() ? "" : ", ") + ContentLabel(ct, a.app_id);
        }
    }
    for (int r = first; r < int(items.size()) && r < first + visible; r++) {
        const auto& c = items[size_t(r)];
        const float y = top + (r - first) * pitch, cy = y + 24;
        // the rows come in one after another when the page opens
        const float in = ui::EaseOut(ui::Appear(m_console_list_at + 0.05 * (r - first), 0.3f));
        nvgSave(vg);
        nvgTranslate(vg, (1 - in) * 30, 0);
        nvgGlobalAlpha(vg, in);
        if (r == m_console_item && m_console_btn == 0) {
            ui::FillRound(vg, x0, y, w, pitch - 6, 10, t.panel2);
            ui::Focus(vg, t, x0, y, w, pitch - 6, 10, m_time);
        } else {
            ui::FillRound(vg, x0, y, w, pitch - 6, 10, t.panel);
        }
        AddHit(x0, y, w, pitch - 6, HitKind::InstalledRow, r);
        const bool on = m_console_marks[size_t(r)] || base_marked;
        draw::TickBox(vg, t, x0 + 16, cy, on, "console:" + std::to_string(c.id) + ":" + std::to_string(c.version));
        nvgSave(vg);
        nvgScissor(vg, x0 + 52, y, w - 52 - 330, pitch);
        float x = ui::Text(vg, x0 + 52, cy, 17, t.text, ContentLabel(c, a.app_id).c_str()) + 12;
        if (NeedsNewer(a, c)) {
            std::snprintf(text, sizeof(text), m.ic_needs_fmt, VersionText(c.required).c_str());
            x = Tag(vg, x, cy, text, t.orange);
        }
        nvgRestore(vg);
        float rx = x0 + w - 16;
        if (checked && (cv.running || cv.done)) {
            // OK in green, Damaged in red, Checking… in blue while it runs
            int state = cv.running ? 0 : -1;
            for (const auto& vc : cv.result) {
                if (vc.key.id == c.id && vc.key.version == c.version) state = vc.bad.empty() ? 1 : 2;
            }
            if (state >= 0) {
                const char* label = state == 0 ? m.ck_wait : state == 1 ? m.ck_ok : m.ck_bad;
                const NVGcolor col = state == 0 ? t.work : state == 1 ? t.ok : t.err;
                const float k = 1.f;
                const float pw = ui::TextWidth(vg, 14, label) + 40, sx = rx - pw;
                nvgGlobalAlpha(vg, in * k);
                NVGcolor bg = col;
                bg.a = 0.18f;
                ui::FillRound(vg, sx, cy - 13, pw, 26, 13, bg);
                if (state == 0) ui::Spinner(vg, sx + 15, cy, 6, col, m_time);
                else if (state == 1) ui::CheckMark(vg, sx + 15, cy, 10, col, 2.4f);
                else {
                    nvgBeginPath(vg);
                    nvgMoveTo(vg, sx + 10, cy - 5);
                    nvgLineTo(vg, sx + 20, cy + 5);
                    nvgMoveTo(vg, sx + 20, cy - 5);
                    nvgLineTo(vg, sx + 10, cy + 5);
                    nvgStrokeColor(vg, col);
                    nvgStrokeWidth(vg, 2.4f);
                    nvgStroke(vg);
                }
                ui::Text(vg, sx + 28, cy, 14, col, label);
                nvgGlobalAlpha(vg, in);
                rx = sx - 14;
            }
        }
        std::snprintf(text, sizeof(text), "%s · %s", c.nand ? s.internal_short : "SD", draw::Size(c.bytes).c_str());
        ui::Text(vg, rx, cy, 15, t.muted, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
    }
    const float fy = top + visible * pitch + 8;
    if (checked && (cv.running || cv.done)) {
        // under the list: how far the check is, then what it found (1A)
        const float k = ui::EaseOutBack(ui::Appear(m_console_check_at, 0.3f));
        nvgSave(vg);
        nvgGlobalAlpha(vg, std::min(1.f, k));
        draw::Panel(vg, t, x0, fy, w, panel_h, 14);
        const char* block = ConsoleRepairBlock(a.app_id);
        if (cv.running) {
            std::snprintf(text, sizeof(text), m.ck_checking_fmt, cv.parts_done + (cv.parts_done < cv.parts ? 1 : 0), cv.parts,
                          draw::Size(cv.bytes_done).c_str(), draw::Size(cv.bytes).c_str());
            ui::Text(vg, x0 + 20, fy + 26, 17, t.text, text);
            ui::Progress(vg, t, x0 + 20, fy + 52, w - 40, 10, cv.bytes ? float(double(cv.bytes_done) / double(cv.bytes)) : 0.f, t.work, m_time);
        } else if (R_FAILED(cv.rc)) {
            ui::Text(vg, x0 + 20, fy + 26, 17, t.err, m.ck_error);
            std::snprintf(text, sizeof(text), "0x%X", cv.rc);
            ui::Text(vg, x0 + 20, fy + 56, 14, t.muted, text);
        } else if (bad_count) {
            std::snprintf(text, sizeof(text), m.ck_bad_fmt, bad_names.c_str());
            nvgSave(vg);
            nvgScissor(vg, x0 + 20, fy, w - 290, panel_h);
            ui::Text(vg, x0 + 20, fy + 26, 17, t.err, text);
            nvgRestore(vg);
            if (!block) {
                ui::Text(vg, x0 + 20, fy + 56, 14, t.muted, m.ck_repair_note);
                // Repair, in focus when the check ends
                const float rbw = 230, rbh = 48, rbx = x0 + w - rbw - 18, rby = fy + 18;
                if (m_console_btn == 2) ui::Focus(vg, t, rbx, rby, rbw, rbh, 12, m_time);
                ui::FillRound(vg, rbx, rby, rbw, rbh, 12, t.work);
                ui::Symbol(vg, t, rbx + 28, rby + rbh / 2, 24, ui::glyph::A, t.on_work, HidNpadButton_A);
                ui::Text(vg, rbx + 50, rby + rbh / 2, 17, t.on_work, m.ck_repair_go);
                AddHit(rbx, rby, rbw, rbh, HitKind::ConsoleButton, 2);
            } else {
                ui::Text(vg, x0 + 20, fy + 56, 14, t.muted, block == m.ck_not_lib ? m.ck_cannot : block);
            }
        } else {
            std::snprintf(text, sizeof(text), m.ck_all_ok_fmt, parts, parts);
            ui::Text(vg, x0 + 20, fy + 26, 17, t.ok, text);
        }
        nvgRestore(vg);
        if (cv.done && bad_count && !block && m_console_check_focus) {
            m_console_check_focus = false;
            m_console_btn = 2;
        }
        if (m_console_btn == 2 && (!cv.done || !bad_count || block)) m_console_btn = 0;
    } else {
        if (m_console_btn == 2) m_console_btn = 0;
        // what goes, how much it frees; the saves stay
        if (base_marked) {
            ui::Text(vg, x0, fy + 6, 15, t.muted, m.ic_with_game);
        }
        std::snprintf(text, sizeof(text), m.ic_marked_fmt, marked, draw::Size(freed).c_str());
        ui::Text(vg, x0, fy + 6 + (base_marked ? 24 : 0), 15, marked ? t.text : t.muted, text);
    }

    // as everywhere: Y marks, + has every action (deleting what is marked among them)
    std::vector<ui::Hint> hints;
    if (checked && cv.running) {
        hints.push_back({ui::glyph::Plus, tt.fm_actions, HidNpadButton_Plus});
        hints.push_back({ui::glyph::B, m.ck_stop, HidNpadButton_B});
        ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
        return;
    }
    if (!items.empty()) {
        hints.push_back({ui::glyph::A, m_console_btn == 1 ? m.pl_play : m_console_btn == 2 ? m.ck_repair_go : tt.cd_hint, HidNpadButton_A});
        hints.push_back({ui::glyph::Y, m.ic_mark, HidNpadButton_Y});
    }
    if (orphan) {
        if (LibraryBase(a.app_id)) hints.push_back({ui::glyph::X, m.og_install, HidNpadButton_X});
    } else if (g) {
        hints.push_back({ui::glyph::X, ms.Any() && (ms.update_lib || ms.dlc_lib) ? tt.ms_install_hint : m.ic_from_library, HidNpadButton_X});
    }
    if (!items.empty()) hints.push_back({ui::glyph::Plus, tt.fm_actions, HidNpadButton_Plus});
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
    // as everywhere: A opens, Y marks, + has every action (deleting what is marked, the filter,
    // the size)
    std::vector<ui::Hint> hints = {{ui::glyph::A, m.ic_open, HidNpadButton_A}, {ui::glyph::Y, m.ic_mark, HidNpadButton_Y}};
    hints.push_back({ui::glyph::Plus, TrT().fm_actions, HidNpadButton_Plus});
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
    m_dialog.keep_saves = m_ui.save_copy; // the box starts as Settings › Saves says
    m_dialog.tick_focus = -1;              // the focus on the buttons, Cancel first
    ConsoleRemoveExtras(m_dialog.ids);
}

void App::UpdateConsole(u64 down, u64 held) {
    if (!m_space) return;
    const auto all = m_space->Snapshot();
    // while several games are taken off, B stops before the next one
    if (all.task == Space::Task::Remove && all.remove_count > 1 && (down & HidNpadButton_B)) {
        StopRemove();
        return;
    }
    if (!m_console_app) {
        // the filter of what is missing (6A), the size and deleting what is marked are in the menu
        // of + (1A, 14/11)
        const auto v = ConsoleView(all);
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
        if (m_ui.console_size != 0) {
            const int cols = m_ui.console_size == 2 ? 4 : 6;
            if (Repeat(down, held, HidNpadButton_AnyLeft) && m_console_focus > 0) m_console_focus--;
            if (Repeat(down, held, HidNpadButton_AnyRight) && m_console_focus < n - 1) m_console_focus++;
            if (Repeat(down, held, HidNpadButton_AnyUp) && m_console_focus >= cols) m_console_focus -= cols;
            if (Repeat(down, held, HidNpadButton_AnyDown) && m_console_focus / cols < (n - 1) / cols) {
                m_console_focus = std::min(n - 1, m_console_focus + cols);
            }
        } else {
            if (Repeat(ListDown(down), ListHeld(held), HidNpadButton_AnyUp) && m_console_focus > 0) m_console_focus--;
            if (Repeat(ListDown(down), ListHeld(held), HidNpadButton_AnyDown) && m_console_focus < n - 1) m_console_focus++;
        }
        // by first letter, for long lists (when they are in the order of their names)
        const bool left = m_ui.console_size == 0 && Repeat(ListDown(down), ListHeld(held), HidNpadButton_AnyLeft),
                   right = m_ui.console_size == 0 && Repeat(ListDown(down), ListHeld(held), HidNpadButton_AnyRight);
        if ((left || right) && m_ui.console_sort == kSortName) {
            std::vector<std::string> names;
            for (const auto& a : v.apps) names.push_back(a.name);
            m_console_focus = ListJump(names, m_console_focus, right ? 1 : -1);
        }
        m_console_focus = std::clamp(m_console_focus, 0, int(v.apps.size()) - 1);
        if (down & HidNpadButton_A) {
            m_console_app = v.apps[size_t(m_console_focus)].app_id;
            m_console_item = 0;
            m_console_btn = 0;
            m_console_marks.clear();
            m_console_list_at = m_time; // its rows come in
        }
        return;
    }
    const auto& v = all;
    const InstalledApp* found = nullptr;
    for (const auto& a : v.apps) {
        if (a.app_id == m_console_app) found = &a;
    }
    const bool checking = m_console_check == m_console_app && m_queue && m_queue->GetVerify().running;
    if (down & HidNpadButton_B) {
        if (checking) {
            m_queue->CancelVerify(); // stops the check; the page stays
            return;
        }
        m_console_app = 0;
        m_console_check = 0;
        m_console_btn = 0;
        return;
    }
    if (!found) return;
    const auto items = Items(*found);
    if (m_console_marks.size() != items.size()) m_console_marks.assign(items.size(), 0);
    // Play over the rows, Repair under them (1A, 2A)
    const bool play = ConsoleCanPlay(*found);
    bool repair = false;
    if (m_console_check == m_console_app && m_queue && !ConsoleRepairBlock(m_console_app)) {
        const auto cv = m_queue->GetVerify();
        if (cv.done && R_SUCCEEDED(cv.rc)) {
            for (const auto& c : cv.result) repair = repair || !c.bad.empty();
        }
    }
    if (Repeat(down, held, HidNpadButton_AnyUp)) {
        if (m_console_btn == 2) m_console_btn = 0;
        else if (m_console_btn == 0 && m_console_item > 0) m_console_item--;
        else if (m_console_btn == 0 && play) m_console_btn = 1;
    }
    if (Repeat(down, held, HidNpadButton_AnyDown)) {
        if (m_console_btn == 1) m_console_btn = 0;
        else if (m_console_btn == 0 && m_console_item < int(items.size()) - 1) m_console_item++;
        else if (m_console_btn == 0 && repair) m_console_btn = 2;
    }
    m_console_item = std::clamp(m_console_item, 0, std::max(0, int(items.size()) - 1));
    if ((down & HidNpadButton_A) && m_console_btn == 1) {
        PlayConsoleGame(false);
        return;
    }
    if ((down & HidNpadButton_A) && m_console_btn == 2) {
        RepairConsoleGame();
        return;
    }
    if ((down & HidNpadButton_Y) && !items.empty()) {
        m_console_btn = 0;
        m_console_marks[size_t(m_console_item)] = !m_console_marks[size_t(m_console_item)];
    }
    if ((down & HidNpadButton_A) && !items.empty()) {
        // the details of the line: its ticket and the version the console asks for (7A)
        OpenContentDetails(*found, items[size_t(m_console_item)]);
        return;
    }
    if (down & HidNpadButton_X) {
        if (WithoutGame(*found)) {
            // an update or a DLC without its game: the game itself from the library (5C)
            InstallConsoleBase();
            return;
        }
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
}

void App::AskConsoleRemoveMarked() {
    if (!m_space || !m_console_app) return;
    const auto v = m_space->Snapshot();
    const InstalledApp* found = nullptr;
    for (const auto& a : v.apps) {
        if (a.app_id == m_console_app) found = &a;
    }
    if (!found) return;
    const auto items = Items(*found);
    if (m_console_marks.size() != items.size()) m_console_marks.assign(items.size(), 0);
    bool base = false;
    for (size_t i = 0; i < items.size(); i++) {
        if (m_console_marks[i] && items[i].type == NcmContentMetaType_Application) base = true;
    }
    // without its game, Remove takes all of it (5C)
    const bool all = WithoutGame(*found);
    m_console_remove.clear();
    for (size_t i = 0; i < items.size(); i++) {
        if (m_console_marks[i] || base || all) m_console_remove.push_back(items[i]);
    }
    if (m_console_remove.empty()) {
        Notice(TrM().ic_nothing);
        return;
    }
    if (m_space->Busy() || (m_queue && (m_queue->Busy() || m_queue->Undoing()))) {
        Notice(TrM().cz_busy);
        return;
    }
    OpenDialog(DialogKind::ConsoleRemove);
    m_dialog.keep_saves = m_ui.save_copy; // the box starts as Settings › Saves says
    m_dialog.tick_focus = -1;              // the focus on the buttons, Cancel first
    m_dialog.text = found->name;
    if (base) ConsoleRemoveExtras({m_console_app});
}

// ---- check, repair and play from the page of a game (1A, 2A), its mods and tickets (7A) ----

bool App::ConsoleCanPlay(const InstalledApp& a) const {
    // the game itself must be there: not archived, not an update or a DLC alone
    return !a.archived && std::any_of(a.contents.begin(), a.contents.end(),
                                      [](const InstalledApp::Content& c) { return c.type == NcmContentMetaType_Application; });
}

bool App::LibraryBase(unsigned long long app) const {
    const Game* g = LibraryGame(app);
    if (!g) return false;
    return std::any_of(g->files.begin(), g->files.end(), [](const CatalogFile& f) { return f.kind == CatalogFile::Kind::Game; });
}

const char* App::ConsoleRepairBlock(unsigned long long app) {
    const More& m = TrM();
    if (!m_link.CurrentConfig().Linked()) return m.ck_no_lib;
    if (!LibraryGame(app)) return m.ck_not_lib;
    return nullptr;
}

void App::CheckConsoleGame() {
    if (!m_queue || !m_console_app) return;
    if (!m_queue->StartVerify(m_console_app)) {
        Notice(Tr().verify_wait); // something installs: the check waits
        return;
    }
    m_console_check = m_console_app;
    m_console_check_at = m_time;
    m_console_check_focus = true;
    m_console_btn = 0;
}

void App::RepairConsoleGame() {
    if (!m_queue || m_console_check != m_console_app) return;
    const auto v = m_queue->GetVerify();
    const Game* g = LibraryGame(m_console_check);
    if (!v.done || !g || ConsoleRepairBlock(m_console_check)) return;
    // only the damaged parts come from the library, as from the page of a game of the library
    std::vector<InstallJob> jobs;
    if (!RepairJobsFor(*g, v, jobs)) {
        Notice(Tr().repair_missing);
        return;
    }
    AddJobs(std::move(jobs));
    NoticeOk(TrM().ck_queued);
    m_console_check = 0;
    m_console_btn = 0;
}

void App::InstallConsoleBase() {
    const Game* g = LibraryGame(m_console_app);
    const CatalogFile* base = nullptr;
    if (g) {
        for (const auto& f : g->files) {
            if (f.kind == CatalogFile::Kind::Game && (!base || f.version > base->version)) base = &f;
        }
    }
    if (!base) {
        Notice(TrM().og_not_lib);
        return;
    }
    AddJobs({JobFor(*g, *base)});
}

void App::PlayConsoleGame(bool asked) {
    const More& m = TrM();
    if (!m_space || !m_console_app) return;
    std::string name;
    for (const auto& a : m_space->Snapshot().apps) {
        if (a.app_id == m_console_app) name = a.name;
    }
    // NXDock closes when the game opens: never while it installs, removes or checks
    if ((m_queue && (m_queue->Busy() || m_queue->Undoing() || m_queue->GetVerify().running)) || m_space->Busy()) {
        Notice(m.cz_busy);
        return;
    }
    if (!asked && !net::IsAppletMode()) {
        // opened inside a game (R held, or the HOME shortcut): that game closes, so it asks first
        OpenDialog(DialogKind::Play);
        m_dialog.text = name;
        m_dialog.ids = {m_console_app};
        return;
    }
    const Result rc = appletRequestLaunchApplication(m_console_app, nullptr);
    if (R_FAILED(rc)) {
        char text[200];
        std::snprintf(text, sizeof(text), m.pl_failed_fmt, unsigned(rc));
        Notice(text);
        return;
    }
    // the console opens the game once NXDock has gone; its name is shown meanwhile
    m_launch_at = m_time;
    m_launch_name = name;
}

void App::DrawLaunch(NVGcontext* vg) {
    if (m_launch_at < 0) return;
    const float k = ui::EaseOut(ui::Appear(m_launch_at, 0.4f));
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.92f * k));
    nvgFill(vg);
    const bool app = !m_launch_text.empty(); // a .nro (22A): no cover of a game
    const Game* g = app ? nullptr : LibraryGame(m_console_app);
    int image = g && !g->cover.empty() ? m_covers.Get(g->cover) : 0;
    if (!image && !app) image = m_covers.Get(IconPath(m_console_app));
    const float s = 140 + 50 * k;
    nvgGlobalAlpha(vg, k);
    ui::Cover(vg, m_theme, kWidth / 2 - s / 2, kHeight / 2 - s / 2 - 30, s, PlaceholderColor(m_launch_name), Initial(m_launch_name).c_str(), nullptr,
              false, false, 22, image);
    char text[300];
    if (app) std::snprintf(text, sizeof(text), "%s", m_launch_text.c_str());
    else std::snprintf(text, sizeof(text), TrM().pl_opening_fmt, m_launch_name.c_str());
    ui::Text(vg, kWidth / 2, kHeight / 2 + s / 2 + 4, 20, nvgRGBf(0.91f, 0.93f, 0.95f), text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgGlobalAlpha(vg, 1.f);
}

void App::ConsoleRemoveExtras(const std::vector<unsigned long long>& apps) {
    // what else of these games is on the SD card and in the console: their mods, their tickets
    m_dialog.remove_extra = false; // off by default: cheats are not lost by mistake
    m_dialog.extra_mods = m_dialog.extra_common = m_dialog.extra_personal = 0;
    for (const auto app : apps) {
        m_dialog.extra_mods += std::max(0, ModFolders(ContentsDir(app)));
        for (const auto& t : TicketsOf(app)) (t.personal ? m_dialog.extra_personal : m_dialog.extra_common)++;
    }
}

void RemoveGameExtras(unsigned long long app) {
    // its folder in atmosphere/contents, and what NXDock wrote down of its mods
    const std::string dir = ContentsDir(app);
    struct stat st{};
    if (stat(dir.c_str(), &st) == 0) {
        fileops::Progress p;
        char id[20];
        std::snprintf(id, sizeof(id), "%016llX", app);
        std::vector<std::string> paths = {dir};
        const std::string state = ModsStateDir() + "/" + id;
        if (stat(state.c_str(), &st) == 0) paths.push_back(state);
        if (!fileops::DeleteAll(paths, p)) log_write("[NXDock] remove: the mods of %016llX did not all go\n", app);
    }
    // its tickets, each saved to the SD card first; one that cannot be saved stays
    int kept = 0;
    SaveAndDeleteTickets(TicketsOf(app), &kept);
    if (kept) log_write("[NXDock] remove: %d tickets of %016llX stay (their copy failed)\n", kept, app);
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
        if (a.archived || WithoutGame(a)) continue; // without its game, nothing is missing of it (5C)
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

bool App::IsShortcut(const InstalledApp& a) {
    unsigned long long program = 0;
    bool update = false, dlc = false;
    for (const auto& c : a.contents) {
        if (c.type == NcmContentMetaType_Application) program += c.bytes;
        update = update || c.type == NcmContentMetaType_Patch;
        dlc = dlc || c.type == NcmContentMetaType_AddOnContent;
    }
    return ShortcutOf(a.app_id, program, update, dlc) != ShortcutSign::None;
}

Space::View App::ConsoleApps(const Space::View& v) const {
    if (m_ui.show_shortcuts) return v;
    Space::View out = v;
    std::erase_if(out.apps, [](const InstalledApp& a) { return IsShortcut(a); });
    return out;
}

Space::View App::ConsoleView(const Space::View& raw) {
    // the shortcuts of homebrew out, unless asked for in +: nothing hidden is marked nor deleted
    const Space::View in = ConsoleApps(raw);
    // the games taken off so far, out of the list at once, not at the end of all
    Space::View trimmed;
    const Space::View* from = &in;
    if (!in.removed.empty()) {
        trimmed = in;
        std::erase_if(trimmed.apps, [&](const InstalledApp& a) { return std::find(in.removed.begin(), in.removed.end(), a.app_id) != in.removed.end(); });
        from = &trimmed;
    }
    const Space::View& v = *from;
    // the filters (what is missing, 6A; without the game, 5C), then the order (4A)
    if (!m_console_missing && !m_console_orphans && m_ui.console_sort == kSortName) return v;
    if (m_console_missing) FollowMissing(v);
    Space::View out = v;
    if (m_console_missing || m_console_orphans) {
        out.apps.clear();
        for (const auto& a : v.apps) {
            if (m_console_orphans ? WithoutGame(a) : (!a.archived && !WithoutGame(a) && MissingOf(a.app_id).Any())) out.apps.push_back(a);
        }
    }
    SortConsole(out.apps, m_ui.console_sort);
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
                        sphaira::es::Exit(); // closed again: es keeps the saves it read open while it is open
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
    OpenDialog(DialogKind::ContentDetails);
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

// The lines of a game on the console, for the menu of actions (more_actions.cpp).
std::vector<InstalledApp::Content> ConsoleItems(const InstalledApp& a) { return Items(a); }


// ---- a game at random (21A of groups 3 and 4) ----

namespace {

constexpr double kDiceTime = 1.6;

} // namespace

void App::RollRandom() {
    const More& m = TrM();
    if (!m_space) return;
    // NXDock closes when the game opens: never while it installs, removes or checks
    if ((m_queue && (m_queue->Busy() || m_queue->Undoing() || m_queue->GetVerify().running)) || m_space->Busy()) {
        Notice(m.cz_busy);
        return;
    }
    const auto v = m_space->Snapshot();
    std::vector<const InstalledApp*> can;
    for (const auto& a : v.apps) {
        if (ConsoleCanPlay(a)) can.push_back(&a);
    }
    if (can.empty()) {
        Notice(m.rg_none);
        return;
    }
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> pick(0, can.size() - 1);
    // the covers that pass, slower and slower, the chosen one last
    m_dice_apps.clear();
    for (int i = 0; i < 14; i++) m_dice_apps.push_back(can[pick(rng)]->app_id);
    const InstalledApp* chosen = can[pick(rng)];
    if (can.size() > 1 && m_dice_apps.size() > 1) {
        while (m_dice_apps[m_dice_apps.size() - 2] == chosen->app_id) m_dice_apps[m_dice_apps.size() - 2] = can[pick(rng)]->app_id;
    }
    m_dice_apps.push_back(chosen->app_id);
    // the question, with what it was played and Another one on Y
    OpenDialog(DialogKind::PlayRandom);
    m_dialog.ids = {chosen->app_id};
    m_dialog.text = chosen->name;
    m_dialog.from = StatsText(*chosen) + " \xC2\xB7 " + WhereOf(*chosen, Tr());
    m_dialog.focus = 1;
    // without animations, the question at once; else the dice first, the question after it
    m_dice_at = ui::Still() ? -100 : m_time;
}

void App::DrawDice(NVGcontext* vg) {
    if (m_dice_at < 0) return;
    const double age = m_time - m_dice_at;
    if (age > kDiceTime + 0.35) {
        m_dice_at = -100;
        m_dialog_at = m_time; // the question comes in now
        return;
    }
    const Theme& t = m_theme;
    const float fade = float(std::min(1.0, age / 0.2) * std::min(1.0, (kDiceTime + 0.35 - age) / 0.25));
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.6f * fade));
    nvgFill(vg);
    nvgGlobalAlpha(vg, fade);
    ui::Text(vg, kWidth / 2, 170, 22, nvgRGBf(0.91f, 0.93f, 0.95f), TrM().rg_choosing, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    // the covers slide from right to left and slow down on the last one
    const int n = int(m_dice_apps.size());
    const float p = float(std::min(1.0, age / kDiceTime));
    const float ease = 1 - (1 - p) * (1 - p) * (1 - p);
    const float pos = ease * float(n - 1); // the cover in the middle, as a fraction
    const float size = 200, gap = 40, cx = kWidth / 2, cy = kHeight / 2 - 10;
    const auto v = m_space ? m_space->Snapshot() : Space::View{};
    for (int i = 0; i < n; i++) {
        const float d = float(i) - pos;
        if (std::fabs(d) > 3.5f) continue;
        const float x = cx + d * (size + gap), s = size * (1 - 0.18f * std::min(1.f, std::fabs(d)));
        std::string name;
        for (const auto& a : v.apps) {
            if (a.app_id == m_dice_apps[size_t(i)]) name = a.name;
        }
        const Game* g = LibraryGame(m_dice_apps[size_t(i)]);
        int image = g && !g->cover.empty() ? m_covers.Get(g->cover) : 0;
        if (!image) image = m_covers.Get(IconPath(m_dice_apps[size_t(i)]));
        nvgSave(vg);
        nvgGlobalAlpha(vg, fade * std::max(0.f, 1 - std::fabs(d) / 3.5f));
        ui::Cover(vg, t, x - s / 2, cy - s / 2, s, PlaceholderColor(name), Initial(name).c_str(), nullptr, false, false, 16, image);
        nvgRestore(vg);
    }
    // the one landed on gets the focus ring
    if (p >= 1) ui::Focus(vg, t, cx - size / 2, cy - size / 2, size, size, 16, m_time);
    // a die that rolls under it
    const float dr = 22, dx = cx, dy = cy + size / 2 + 50;
    nvgSave(vg);
    nvgTranslate(vg, dx, dy);
    nvgRotate(vg, float((1 - ease) * 12.0));
    ui::FillRound(vg, -dr, -dr, 2 * dr, 2 * dr, 8, nvgRGBf(0.96f, 0.97f, 0.98f));
    const int face = p >= 1 ? 5 : 1 + int(age * 14) % 6;
    const float pip = 4.2f, o = 10;
    const auto dot = [&](float px, float py) {
        nvgBeginPath(vg);
        nvgCircle(vg, px, py, pip);
        nvgFillColor(vg, t.work);
        nvgFill(vg);
    };
    if (face % 2) dot(0, 0);
    if (face >= 2) {
        dot(-o, -o);
        dot(o, o);
    }
    if (face >= 4) {
        dot(o, -o);
        dot(-o, o);
    }
    if (face == 6) {
        dot(-o, 0);
        dot(o, 0);
    }
    nvgRestore(vg);
    nvgGlobalAlpha(vg, 1.f);
}

} // namespace nxdock
