// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The page of a game: its files and versions, where it installs and the room it needs.
#include "app_shared.hpp"

namespace nxdock {

using namespace appui;

namespace {

// The firmware a title needs, from its required system version: 15.0.0.
std::string FirmwareText(u32 version) {
    if (version == 0) {
        return "";
    }
    char text[24];
    std::snprintf(text, sizeof(text), "%u.%u.%u", version >> 26, (version >> 20) & 0x3F, (version >> 16) & 0xF);
    return text;
}

} // namespace

void App::FollowPreflight() {
    if (!m_preflight.valid() || m_preflight.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto jobs = m_preflight.get();
    if (m_dialog.kind == DialogKind::Preflight) m_dialog = Dialog{};
    if (m_preflight_dropped) return;
    AddJobs(std::move(jobs));
}

void App::DestCards(NVGcontext* vg, float x, float y, float w, float h, bool nand, unsigned long long need, HitKind kind) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const float gap = 12, cw = (w - gap) / 2;
    for (int d = 0; d < 2; d++) {
        const float cx = x + d * (cw + gap);
        const bool on = (d == 1) == nand;
        NVGcolor bg = t.work;
        bg.a = on ? 0.14f : 0.f;
        ui::FillRound(vg, cx, y, cw, h, 14, t.panel);
        if (on) ui::FillRound(vg, cx, y, cw, h, 14, bg);
        ui::StrokeRound(vg, cx + 1, y + 1, cw - 2, h - 2, 13, on ? t.work : t.line, on ? 2.5f : 1.f);
        AddHit(cx, y - 4, cw, h + 8, kind, d);
        // the sign: SD, or the chip of the internal memory
        const float ix = cx + 14, iy = y + (h - 40) / 2;
        ui::FillRound(vg, ix, iy, 40, 40, 9, on ? t.work : t.panel2);
        if (d == 0) {
            ui::Text(vg, ix + 20, iy + 21, 15, on ? t.on_work : t.muted, "SD", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        } else {
            ui::StrokeRound(vg, ix + 11, iy + 11, 18, 18, 3, on ? t.on_work : t.muted, 2.f);
            ui::FillRound(vg, ix + 16, iy + 16, 8, 8, 1, on ? t.on_work : t.muted);
        }
        const float tx = ix + 54;
        ui::Text(vg, tx, y + h / 2 - 10, 18, on ? t.text : t.muted, d == 0 ? s.sd_card : s.internal);
        const s64 free_b = d == 0 ? m_sd_free_bytes : m_nand_free_bytes;
        char line[160] = "";
        NVGcolor col = t.muted;
        if (free_b >= 0) {
            if (on && need > 0) {
                const u64 left = u64(free_b) > need ? u64(free_b) - need : 0;
                std::snprintf(line, sizeof(line), m.dc_left_fmt, SizeText(u64(free_b)).c_str(), SizeText(left).c_str());
                if (u64(free_b) < need) col = t.err;
            } else {
                std::snprintf(line, sizeof(line), m.dc_free_fmt, SizeText(u64(free_b)).c_str());
            }
        }
        nvgSave(vg);
        nvgIntersectScissor(vg, tx, y, cw - (tx - cx) - 44, h);
        ui::Text(vg, tx, y + h / 2 + 13, 13, col, line);
        nvgRestore(vg);
        // the round mark of the one chosen
        const float rx = cx + cw - 26, ry = y + h / 2;
        nvgBeginPath(vg);
        nvgCircle(vg, rx, ry, on ? 7.5f : 10);
        nvgStrokeColor(vg, on ? t.work : t.muted);
        nvgStrokeWidth(vg, on ? 6.f : 2.f);
        nvgStroke(vg);
    }
}

// ---- the SD card without room (12A of the visual test of groups 3 and 4) ----

bool App::NoRoomShown(unsigned long long need, bool nand) const {
    // the SD card stays the default: the line only says it, and offers the internal memory once
    return !nand && need > 0 && m_sd_free_bytes >= 0 && need > u64(m_sd_free_bytes);
}

bool App::NoRoomAppears(int screen, unsigned long long need, bool nand) {
    std::string& key = m_noroom_key[std::clamp(screen, 0, 2)];
    if (!NoRoomShown(need, nand)) {
        key.clear();
        return false;
    }
    const std::string now = std::to_string(need);
    if (key == now) return false;
    const bool first = key.empty();
    key = now;
    if (first) m_noroom_at[std::clamp(screen, 0, 2)] = m_time;
    return true;
}

float App::DrawNoRoom(NVGcontext* vg, float x, float y, float w, unsigned long long need, bool focused, int screen) {
    const Theme& t = m_theme;
    const More& m = TrM();
    const float k = ui::EaseOutBack(ui::Appear(m_noroom_at[std::clamp(screen, 0, 2)], 0.4f));
    const float h = 52;
    const u64 free_sd = m_sd_free_bytes > 0 ? u64(m_sd_free_bytes) : 0;
    const bool fits = m_nand_free_bytes >= 0 && need + kNandMargin <= u64(m_nand_free_bytes);
    char line[200], ask[200];
    std::snprintf(line, sizeof(line), m.nr_missing_fmt, SizeText(need > free_sd ? need - free_sd : 0).c_str());
    if (fits) std::snprintf(ask, sizeof(ask), m.nr_ask_fmt, SizeText(u64(m_nand_free_bytes)).c_str());
    else std::snprintf(ask, sizeof(ask), "%s", m.nr_neither);
    nvgSave(vg);
    // it opens down from under the cards
    nvgIntersectScissor(vg, x - 8, y - 4, w + 16, (h + 8) * std::clamp(k, 0.f, 1.05f));
    nvgGlobalAlpha(vg, std::clamp(ui::Appear(m_noroom_at[std::clamp(screen, 0, 2)], 0.25f), 0.f, 1.f));
    NVGcolor fill = t.orange;
    fill.a = 0.12f;
    ui::FillRound(vg, x, y, w, h, 12, t.panel);
    ui::FillRound(vg, x, y, w, h, 12, fill);
    NVGcolor edge = t.orange;
    edge.a = 0.6f;
    ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, 12, edge, 1.2f);
    // a warning triangle, in orange
    const float wx = x + 24, wy = y + h / 2;
    nvgBeginPath(vg);
    nvgMoveTo(vg, wx, wy - 10);
    nvgLineTo(vg, wx + 11, wy + 9);
    nvgLineTo(vg, wx - 11, wy + 9);
    nvgClosePath(vg);
    nvgFillColor(vg, t.orange);
    nvgFill(vg);
    ui::Text(vg, wx, wy + 2, 13, t.panel, "!", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    float right = x + w - 8;
    if (fits) {
        const float bw = ui::TextWidth(vg, 16, m.nr_use) + 40, bx = right - bw;
        if (focused) ui::Focus(vg, t, bx, y + 6, bw, h - 12, 10, m_time);
        ui::FillRound(vg, bx, y + 6, bw, h - 12, 10, t.work);
        ui::Text(vg, bx + bw / 2, y + h / 2, 16, t.on_work, m.nr_use, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        AddHit(bx, y - 2, bw, h + 4, HitKind::NoRoomUse, screen);
        right = bx - 12;
    }
    nvgSave(vg);
    nvgIntersectScissor(vg, x + 44, y, std::max(40.f, right - x - 44), h);
    ui::Text(vg, x + 46, y + 17, 15, t.orange, line);
    ui::Text(vg, x + 46, y + 36, 13, t.muted, ask);
    nvgRestore(vg);
    nvgRestore(vg);
    return h;
}

void App::UseNandOnce(int screen) {
    // the internal memory for this install only; the next one goes to the SD card again
    if (screen == 0) {
        m_to_nand = true;
        m_dest_touched = true;
    } else if (screen == 1) {
        m_files_nand = true;
        m_files_noroom = false;
    } else {
        m_card_nand = true;
    }
}

void App::OpenDetail(int view_index) {
    if (view_index < 0 || view_index >= int(m_view.size())) {
        return;
    }
    m_detail = true;
    m_touch_focus = false;
    m_detail_game = m_view[view_index];
    m_detail_from_console = false;
    m_detail_first = 0;
    // where, as chosen in the wizard: the internal memory still asks every time (EnqueuePicked)
    m_to_nand = false; // the SD card, always; the internal memory only when chosen for this install
    m_dest_touched = false;
    // what is missing is marked: the game if not installed, a newer update, the DLC not installed;
    // or, as chosen in the wizard, only the game, or the game and its update
    const Game& g = m_games[m_detail_game];
    m_detail_pick.assign(g.files.size(), 0);
    const CatalogFile* newest = g.NewestUpdate();
    for (size_t i = 0; i < g.files.size(); i++) {
        const CatalogFile& f = g.files[i];
        switch (f.kind) {
            case CatalogFile::Kind::Game: m_detail_pick[i] = !g.installed.base; break;
            case CatalogFile::Kind::Update: m_detail_pick[i] = m_ui.pick != 1 && &f == newest && f.version > g.installed.patch; break;
            case CatalogFile::Kind::Dlc: m_detail_pick[i] = m_ui.pick == 0 && !g.installed.dlc.count(f.title_id); break;
        }
    }
    // what is missing is already marked: the focus starts on Install, after the rows
    int rows = 0;
    for (const auto& f : g.files) rows += f.kind != CatalogFile::Kind::Update || &f == newest;
    m_detail_focus = rows;
}

void App::UpdateDetail(u64 down, u64 held) {
    const Game& g = m_games[m_detail_game];
    // the rows: the files shown (the game, the newest update, every DLC), then Install
    std::vector<int> rows;
    const CatalogFile* newest = g.NewestUpdate();
    for (int i = 0; i < int(g.files.size()); i++) {
        if (g.files[i].kind != CatalogFile::Kind::Update || &g.files[i] == newest) rows.push_back(i);
    }
    const int count = int(rows.size()) + 1;
    // the button of the SD card without room (12A) sits between the rows and Install
    u64 picked = 0;
    for (size_t i = 0; i < g.files.size() && i < m_detail_pick.size(); i++) {
        if (m_detail_pick[i]) picked += g.files[i].size;
    }
    const bool noroom = NoRoomShown(picked, m_to_nand);
    const int use = int(rows.size()) + 1;
    if (m_detail_focus == use && !noroom) m_detail_focus = int(rows.size());
    if (Repeat(down, held, HidNpadButton_AnyUp)) {
        if (m_detail_focus == use) m_detail_focus = std::max(0, int(rows.size()) - 1);
        else if (m_detail_focus == int(rows.size()) && noroom) m_detail_focus = use;
        else if (m_detail_focus > 0) m_detail_focus--;
    }
    if (Repeat(down, held, HidNpadButton_AnyDown)) {
        if (m_detail_focus == use) m_detail_focus = int(rows.size());
        else if (m_detail_focus == int(rows.size()) - 1 && noroom) m_detail_focus = use;
        else if (m_detail_focus < count - 1) m_detail_focus++;
    }
    if ((down & HidNpadButton_A) && m_detail_focus == use) {
        UseNandOnce(0);
        m_detail_focus = int(rows.size()); // then Install, which says where
        return;
    }
    if (down & (HidNpadButton_ZL | HidNpadButton_ZR)) {
        // the SD card or the internal memory, for this install only (it asks before going there)
        m_to_nand = !m_to_nand;
        m_dest_touched = true;
    }
    if (down & HidNpadButton_A) {
        if (m_detail_focus < int(rows.size())) {
            m_detail_pick[rows[m_detail_focus]] ^= 1;
        } else {
            EnqueuePicked();
        }
    }
    if ((down & HidNpadButton_Y) && (g.installed.base || !g.installed.dlc.empty())) {
        if (m_queue->StartVerify(g.title_id)) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::Verify;
            m_dialog.game = m_detail_game;
            m_dialog.single = true;
        } else {
            Notice(Tr().verify_wait);
        }
    }
    if ((down & HidNpadButton_X) && g.installed.base && !m_shop_active) {
        // the cheats and mods of this game; B comes back here
        OpenMods();
        OpenModsGame(g.title_id, g.name);
        m_mods_from_detail = true;
        m_tab = Tab::More;
        return;
    }
    if (down & HidNpadButton_B) {
        m_detail = false;
        if (m_detail_from_console) {
            // opened from In the console: back there
            m_detail_from_console = false;
            m_tab = Tab::Installed;
        }
    }
}

void App::DrawDetail(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const Game& g = m_games[m_detail_game];
    const float top = 72, bottom = 664;

    (void)bottom; // the cover itself, soft, is behind everything (DrawBackdrop, 6A)
    // 7A: its parts come in one after another, from the right; gentle animations only fade them
    const auto in = [&](int i) { return ui::EaseOut(ui::Appear(m_detail_at + 0.06 + 0.04 * i, 0.34f)); };
    const auto enter = [&](int i) {
        nvgSave(vg);
        const float k = in(i);
        if (ui::Lively()) nvgTranslate(vg, 26 * (1 - k), 0);
        nvgGlobalAlpha(vg, k);
    };

    // the cover
    const float cs = 330, cx = 48, cy = top + 34;
    if (m_time - m_detail_at >= 0.3) { // while the page opens, the cover is still on its way (DrawTransitions)
        nvgBeginPath(vg);
        nvgRoundedRect(vg, cx + 4, cy + 14, cs - 8, cs, 14);
        nvgFillPaint(vg, nvgBoxGradient(vg, cx, cy + 14, cs, cs, 14, 30, nvgRGBAf(0, 0, 0, 0.45f), nvgRGBAf(0, 0, 0, 0)));
        nvgFill(vg);
        ui::Cover(vg, t, cx, cy, cs, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 14, CoverOf(g));
        CoverLoading(vg, cx, cy, cs, g.cover);
        // its reflection on the floor, fading away in thin bands (6A)
        const float ry = cy + cs + 10, rh = 120, band = 10;
        const int image = CoverOf(g);
        for (int b = 0; b < int(rh / band); b++) {
            nvgSave(vg);
            nvgIntersectScissor(vg, cx, ry + b * band, cs, band);
            nvgGlobalAlpha(vg, (t.dark ? 0.24f : 0.3f) * (1 - float(b) / (rh / band)));
            nvgTranslate(vg, 0, 2 * ry);
            nvgScale(vg, 1, -1);
            ui::Cover(vg, t, cx, ry, cs, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 14, image);
            nvgRestore(vg);
        }
    }

    // name and facts
    const float rx = cx + cs + 44, rw = kWidth - rx - 48;
    enter(0);
    nvgSave(vg);
    nvgScissor(vg, rx, cy - 4, rw, 64);
    ui::Title(vg, rx, cy + 26, 36, t.text, g.name.c_str());
    nvgRestore(vg);
    nvgRestore(vg);
    const CatalogFile* newest = g.NewestUpdate();
    u64 picked_size = 0;
    for (size_t i = 0; i < g.files.size(); i++) {
        if (m_detail_pick[i]) picked_size += g.files[i].size;
    }
    u32 firmware = 0;
    for (const auto& f : g.files) {
        if (f.kind != CatalogFile::Kind::Dlc) firmware = std::max(firmware, f.required_system); // a DLC's is its game's version
    }
    struct Fact { const char* label; std::string value; NVGcolor color; };
    std::vector<Fact> facts;
    if (newest) {
        facts.push_back({s.version, VersionText(newest->version), t.text});
    }
    facts.push_back({s.format, FormatOf(g), t.text});
    facts.push_back({s.will_take, SizeText(picked_size), t.text});
    if (firmware) facts.push_back({s.firmware, FirmwareText(firmware) + " " + s.or_newer, t.text});
    const bool versions = g.installed.base && !m_shop_active && !VersionRows(g).empty();
    if (!m_marks_read) {
        m_marks_read = true;
        m_compressed_marks = Compressor::Marks();
    }
    const bool compressed = std::any_of(m_compressed_marks.begin(), m_compressed_marks.end(), [&g](const CompressedGame& c) {
        return c.app_id == g.title_id;
    });
    if (g.installed.base) {
        facts.push_back({s.on_console, VersionText(g.installed.patch), t.ok});
    } else {
        facts.push_back({s.on_console, "—", t.muted});
    }
    if (compressed && g.installed.base) {
        // 24A: what it saved
        u64 saved = 0;
        for (const auto& c : m_compressed_marks) {
            if (c.app_id == g.title_id) saved = c.saved;
        }
        char text[96];
        std::snprintf(text, sizeof(text), TrM().cz_saves_fmt, SizeText(saved).c_str());
        facts.insert(facts.end() - 1, {TrM().cz_badge, text, t.work});
    }
    enter(1);
    float fx = rx;
    const float fy = cy + 62;
    for (size_t i = 0; i < facts.size(); i++) {
        const auto& f = facts[i];
        const bool chip = versions && i + 1 == facts.size(); // the version on the console: + changes it (20A, 1A)
        const float w = std::max(ui::TextWidth(vg, 14, f.label), ui::TextWidth(vg, 17, f.value.c_str())) + 28 + (chip ? 34 : 0);
        ui::FillRound(vg, fx, fy, w, 56, 10, Hex(0, t.dark ? 0.25f : 0.04f));
        ui::StrokeRound(vg, fx + .5f, fy + .5f, w - 1, 55, 10, chip ? t.work : t.line, chip ? 1.5f : 1.f);
        ui::Text(vg, fx + 14, fy + 18, 14, t.muted, f.label);
        ui::Text(vg, fx + 14, fy + 39, 17, f.color, f.value.c_str());
        if (chip) {
            ui::Symbol(vg, t, fx + w - 22, fy + 28, 18, ui::glyph::Plus, t.work, HidNpadButton_Plus);
            AddHit(fx, fy, w, 56, HitKind::VersionChip, 0);
        }
        fx += w + 10;
    }
    nvgRestore(vg);

    // what to install: the game, the newest update, every DLC
    std::vector<int> rows;
    for (int i = 0; i < int(g.files.size()); i++) {
        if (g.files[i].kind != CatalogFile::Kind::Update || &g.files[i] == newest) rows.push_back(i);
    }
    float ly = fy + 76;
    const float lh = 46;
    // the SD card without room (12A): a line under the cards, its button in focus when it appears
    if (NoRoomAppears(0, picked_size, m_to_nand)) m_detail_focus = int(rows.size()) + 1;
    const bool noroom = NoRoomShown(picked_size, m_to_nand);
    if (!noroom && m_detail_focus > int(rows.size())) m_detail_focus = int(rows.size());
    const float strip = noroom ? 60.f : 0.f;
    // room below for where it goes (two large cards) and Install
    const float list_h = std::min({float(rows.size()) * lh + 12, 250.f, std::max(2 * lh + 12, 664 - 14 - 52 - 18 - 64 - 16 - strip - ly)});
    enter(2);
    ui::Glass(vg, t, rx, ly, rw, list_h, 12);
    ListArea(rx, ly, rw, list_h, lh);
    nvgSave(vg);
    nvgScissor(vg, rx, ly, rw, list_h);
    // keep the row in focus in sight; on Install the list stays where it was
    const int visible = std::max(1, int((list_h - 12) / lh));
    if (m_detail_focus < int(rows.size())) {
        if (m_detail_focus < m_detail_first) m_detail_first = m_detail_focus;
        if (m_detail_focus >= m_detail_first + visible) m_detail_first = m_detail_focus - visible + 1;
    }
    m_detail_first = std::clamp(m_detail_first, 0, std::max(0, int(rows.size()) - visible));
    const int first = m_detail_first;
    for (int r = first; r < int(rows.size()); r++) {
        const CatalogFile& f = g.files[rows[r]];
        const float y = ly + 6 + (r - first) * lh;
        if (r == m_detail_focus) {
            ui::FillRound(vg, rx + 6, y, rw - 12, lh - 4, 8, t.panel2);
            ui::StrokeRound(vg, rx + 6, y, rw - 12, lh - 4, 8, t.work, 3);
        }
        AddHit(rx + 6, y, rw - 12, lh - 4, HitKind::DetailRow, r);
        const bool on = m_detail_pick[rows[r]];
        ui::FillRound(vg, rx + 20, y + 9, 24, 24, 6, on ? t.work : Hex(0, 0));
        if (on) {
            ui::CheckMark(vg, rx + 32, y + 21, 13, t.on_work, 2.4f);
        } else {
            ui::StrokeRound(vg, rx + 20.5f, y + 9.5f, 23, 23, 6, t.muted, 1.5f);
        }
        std::string label;
        bool installed = false, fresh = false;
        switch (f.kind) {
            case CatalogFile::Kind::Game: label = s.base_game; installed = g.installed.base; break;
            case CatalogFile::Kind::Update:
                label = VersionText(f.version);
                fresh = g.installed.base && f.version > g.installed.patch;
                installed = g.installed.base && f.version <= g.installed.patch;
                break;
            case CatalogFile::Kind::Dlc:
                label = s.dlc_prefix + (f.dlc_name.empty() ? f.name : f.dlc_name);
                installed = g.installed.dlc.count(f.title_id) > 0;
                break;
        }
        nvgSave(vg);
        nvgIntersectScissor(vg, rx + 56, y, rw - 56 - 200, lh);
        const float tw = ui::Text(vg, rx + 56, y + 21, 17, t.text, label.c_str()) - (rx + 56);
        nvgRestore(vg);
        if (fresh || installed) {
            const char* tag = fresh ? s.new_tag : s.installed_tag;
            const NVGcolor color = fresh ? t.cyan : t.ok;
            const float tx = rx + 56 + std::min(tw, rw - 270) + 10, tagw = ui::TextWidth(vg, 12, tag) + 14;
            ui::StrokeRound(vg, tx, y + 10, tagw, 22, 6, color, 1);
            ui::Text(vg, tx + tagw / 2, y + 21, 12, color, tag, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        ui::Text(vg, rx + rw - 18, y + 21, 15, t.muted, (Upper(f.format) + " · " + SizeText(f.size)).c_str(),
                 NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    nvgRestore(vg);
    nvgRestore(vg);

    // where, in large (the SD card unless chosen otherwise), and Install saying where
    const float cards_y = ly + list_h + 16;
    enter(3);
    DestCards(vg, rx, cards_y, rw, 64, m_to_nand, picked_size, HitKind::Dest);
    nvgRestore(vg);
    if (noroom) DrawNoRoom(vg, rx, cards_y + 64 + 8, rw, picked_size, m_detail_focus == int(rows.size()) + 1, 0);
    const float dy = cards_y + 64 + 18 + strip;
    char install[160];
    std::snprintf(install, sizeof(install), m_to_nand ? TrM().dc_install_nand_fmt : TrM().dc_install_sd_fmt, SizeText(picked_size).c_str());
    const float bw = ui::TextWidth(vg, 20, install) + 64, bx = kWidth - 48 - bw;
    // under the cards, in the room left of Install: it never runs under the button
    enter(4);
    const float k4 = in(4);
    if (m_to_nand) TextBox(vg, rx, dy - 2, std::max(120.f, bx - rx - 20), 14, t.orange, TrM().dc_once);
    const bool can = picked_size > 0;
    if (m_detail_focus == int(rows.size())) {
        ui::Focus(vg, t, bx, dy - 4, bw, 52, 12, m_time);
    }
    nvgGlobalAlpha(vg, (can ? 1.f : 0.45f) * k4);
    ui::FillRound(vg, bx, dy - 4, bw, 52, 12, t.work);
    ui::Text(vg, bx + bw / 2, dy + 22, 20, t.on_work, install, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
    AddHit(bx, dy - 4, bw, 52, HitKind::Install, int(rows.size()));
    static const std::string where_glyphs = std::string(ui::glyph::ZL) + " " + ui::glyph::ZR;
    std::vector<ui::Hint> hints = {{ui::glyph::A, s.select, HidNpadButton_A}, {where_glyphs.c_str(), TrM().dc_switch, HidNpadButton_ZR}};
    if (g.installed.base || !g.installed.dlc.empty()) {
        hints.push_back({ui::glyph::Y, s.verify, HidNpadButton_Y});
    }
    if (g.installed.base && !m_shop_active) {
        hints.push_back({ui::glyph::X, TrM().t_mods, HidNpadButton_X}); // its cheats and mods (10A)
    }
    hints.push_back({ui::glyph::Plus, TrT().fm_actions, HidNpadButton_Plus}); // all of it, another version too (20A, 1A)
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

std::vector<int> App::VersionRows(const Game& g) const {
    std::vector<int> rows;
    for (int i = 0; i < int(g.files.size()); i++) {
        if (g.files[i].kind == CatalogFile::Kind::Update) rows.push_back(i);
    }
    std::sort(rows.begin(), rows.end(), [&g](int a, int b) { return g.files[a].version > g.files[b].version; });
    return rows;
}

void App::OpenVersions() {
    const Game& g = m_games[m_detail_game];
    const auto rows = VersionRows(g);
    if (rows.empty() || !g.installed.base) return;
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::Versions;
    m_dialog.game = m_detail_game;
    m_dialog.focus = 1;
    // the focus on the version installed, or the newest
    m_dialog.entry = 0;
    for (int r = 0; r < int(rows.size()); r++) {
        if (g.files[rows[r]].version == g.installed.patch) m_dialog.entry = r;
    }
}

} // namespace nxdock
