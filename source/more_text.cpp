// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Opening files of the explorer with A (20A and 22A of the visual test of groups 3 and 4):
//  - a .txt, .log, .ini, .json, .cfg or .md in a text viewer, read only, over the whole screen:
//    numbered lines, the stick, the pad or a finger to scroll, and in + wrapping the lines, the
//    end and the top. At most kTextCap bytes are read, and a cut file says so;
//  - a .nro after a short question, as NXDock closes to open it. When the queue is working it says
//    so in orange: what installs and waits stops, and is written down to go on when NXDock opens
//    again (those from the library or a disk; the computer and the shops cannot).
#include "app.hpp"

#include "draw.hpp"
#include "fileops.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace nxdock {

namespace {

constexpr float kViewX = 40, kViewY = 128, kViewW = 1200, kViewH = 524, kPitch = 25, kFont = 16;

int Digits(int n) {
    int d = 1;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

} // namespace

// ---- the text viewer (20A) ----

void App::OpenViewer(const LocalEntry& e) {
    m_text = ReadText(e.path);
    if (!m_text.ok) {
        Notice(TrM().tv_failed);
        return;
    }
    m_text_name = e.name;
    m_text_path = e.path;
    m_text_rows.clear();
    m_text_cols = 0; // the rows are made when it is drawn, at its width
    m_text_top = 0;
    m_more = MoreScreen::Viewer;
}

void App::DrawViewer(NVGcontext* vg) {
    const Theme& t = m_theme;
    const More& m = TrM();
    // the name, "read only", and where it is
    nvgSave(vg);
    nvgScissor(vg, kViewX + 8, 82, 760, 40);
    const float nx = ui::Title(vg, kViewX + 8, 104, 22, t.text, m_text_name.c_str());
    nvgRestore(vg);
    {
        const float x = std::min(nx + 14, kViewX + 780.f), w = ui::TextWidth(vg, 13, m.tv_read_only) + 20;
        NVGcolor bg = t.muted;
        bg.a = 0.18f;
        ui::FillRound(vg, x, 93, w, 22, 11, bg);
        ui::Text(vg, x + w / 2, 104, 13, t.muted, m.tv_read_only, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    draw::Panel(vg, t, kViewX, kViewY, kViewW, kViewH, 14);
    // the width of a character of the font, as near as a font of one width can be had
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, kFont);
    const char* sample = "0123456789abcdefghijklmnopqrstuvwxyz";
    const float cw = ui::TextWidth(vg, kFont, sample) / float(std::strlen(sample)) * 1.08f;
    const int lines = int(m_text.lines.size());
    const float gutter = float(Digits(std::max(1, lines))) * cw + 24;
    const float tx = kViewX + 16 + gutter, tw = kViewW - 32 - gutter - 14;
    const int cols = std::max(8, int(tw / std::max(4.f, cw)));
    if (cols != m_text_cols) {
        // made again at a new width, or with wrapping changed: the line at the top stays there
        const int line = m_text_top < int(m_text_rows.size()) ? m_text_rows[size_t(m_text_top)].line : 0;
        m_text_rows = TextRows(m_text.lines, cols, m_ui.text_wrap);
        m_text_cols = cols;
        m_text_top = 0;
        for (size_t i = 0; i < m_text_rows.size(); i++) {
            if (m_text_rows[i].line >= line) {
                m_text_top = int(i);
                break;
            }
        }
    }
    const float bottom_note = m_text.cut ? 30.f : 0.f;
    const int visible = std::max(1, int((kViewH - 20 - bottom_note) / kPitch));
    const int rows = int(m_text_rows.size());
    m_text_top = std::clamp(m_text_top, 0, std::max(0, rows - visible));
    // the gutter with the numbers of the lines
    nvgBeginPath(vg);
    nvgRect(vg, kViewX + 16 + gutter - 10, kViewY + 10, 1, kViewH - 20 - bottom_note);
    nvgFillColor(vg, t.line);
    nvgFill(vg);
    ListArea(kViewX, kViewY, kViewW, kViewH, kPitch); // a finger drags it row by row
    AddHit(kViewX, kViewY, kViewW, kViewH, HitKind::ViewerArea);
    nvgSave(vg);
    nvgScissor(vg, kViewX + 8, kViewY + 8, kViewW - 30, kViewH - 16 - bottom_note);
    const float in = ui::EaseOut(ui::Appear(m_more_at, 0.3f));
    nvgGlobalAlpha(vg, in);
    for (int r = 0; r < visible && m_text_top + r < rows; r++) {
        const TextRow& row = m_text_rows[size_t(m_text_top + r)];
        const float cy = kViewY + 10 + kPitch * (float(r) + 0.5f) + (1 - in) * 10;
        if (row.first) {
            char num[16];
            std::snprintf(num, sizeof(num), "%d", row.line + 1);
            ui::Text(vg, kViewX + 16 + gutter - 20, cy, 14, t.muted, num, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        nvgSave(vg);
        nvgIntersectScissor(vg, tx, cy - kPitch / 2, tw, kPitch);
        ui::Text(vg, tx, cy, kFont, t.text, row.text.c_str());
        nvgRestore(vg);
    }
    nvgRestore(vg);
    // where it is: a bar on the right, as long as what is in sight
    if (rows > visible) {
        const float bh = kViewH - 24 - bottom_note, len = std::max(30.f, bh * float(visible) / float(rows));
        const float at = (bh - len) * float(m_text_top) / float(std::max(1, rows - visible));
        NVGcolor track = t.muted;
        track.a = 0.18f;
        ui::FillRound(vg, kViewX + kViewW - 14, kViewY + 12, 5, bh, 2.5f, track);
        ui::FillRound(vg, kViewX + kViewW - 14, kViewY + 12 + at, 5, len, 2.5f, t.work);
    }
    if (m_text.cut) {
        char text[200];
        std::snprintf(text, sizeof(text), m.tv_cut_fmt, draw::Size(kTextCap).c_str(), draw::Size(m_text.size).c_str());
        ui::Text(vg, kViewX + 18, kViewY + kViewH - 18, 14, t.orange, text);
    }
    {
        // "Line 7 of 38": the line at the top of what is in sight
        char text[80];
        const int line = rows ? m_text_rows[size_t(m_text_top)].line + 1 : 0;
        std::snprintf(text, sizeof(text), m.tv_line_fmt, line, lines);
        ui::Text(vg, kViewX + kViewW - 8, 104, 15, t.muted, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    std::vector<ui::Hint> hints = {{ui::glyph::DPad, m.tv_scroll, 0},
                                   {ui::glyph::Plus, TrT().fm_actions, HidNpadButton_Plus},
                                   {ui::glyph::B, Tr().close, HidNpadButton_B}};
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::UpdateViewer(u64 down, u64 held) {
    const int rows = int(m_text_rows.size());
    const int page = std::max(1, int((kViewH - 20 - (m_text.cut ? 30.f : 0.f)) / kPitch) - 1);
    // a row with the pad or the stick (or a finger), a page with left and right, L and R stay the tabs
    if (Repeat(down, held, HidNpadButton_AnyUp)) m_text_top--;
    if (Repeat(down, held, HidNpadButton_AnyDown)) m_text_top++;
    if (Repeat(down, held, HidNpadButton_AnyLeft)) m_text_top -= page;
    if (Repeat(down, held, HidNpadButton_AnyRight)) m_text_top += page;
    if (down & HidNpadButton_ZL) m_text_top = 0;
    if (down & HidNpadButton_ZR) m_text_top = rows;
    m_text_top = std::clamp(m_text_top, 0, std::max(0, rows - 1)); // the end is clamped again when drawn
    if (down & HidNpadButton_B) {
        m_more = MoreScreen::Files;
        m_text = TextFile{};
        m_text_rows.clear();
        m_list_at = -100; // the list as it was
    }
}

// ---- opening a .nro (22A) ----

void App::AskOpenNro(const LocalEntry& e) {
    const More& m = TrM();
    const auto guard = fileops::GuardOf(e.path);
    if (guard == fileops::Guard::System || guard == fileops::Guard::Self || e.path == m_home.nro) {
        Notice(m.nro_locked);
        return;
    }
    // the homebrew menu opens only what is on the SD card, and only when NXDock came from it
    if (e.path.rfind("sdmc:/", 0) != 0 || !envHasNextLoad()) {
        Notice(m.nro_sd_only);
        return;
    }
    // its name from its NACP, else its file name
    m_nro_path = e.path;
    m_nro_name = e.name.size() > 4 ? e.name.substr(0, e.name.size() - 4) : e.name;
    std::vector<u8> icon;
    auto nacp = std::make_unique<NacpStruct>();
    if (forwarder::NroAssets(e.path, icon, *nacp)) {
        NacpLanguageEntry* lang = nullptr;
        if (R_SUCCEEDED(nacpGetLanguageEntry(nacp.get(), &lang)) && lang && lang->name[0]) m_nro_name = lang->name;
    }
    OpenDialog(DialogKind::OpenNro);
}

void App::OpenNro() {
    if (m_nro_path.empty() || !envHasNextLoad()) {
        Notice(TrM().nro_sd_only);
        return;
    }
    // the queue is written down when NXDock leaves (Exit), to go on the next time
    m_keep_queue = m_queue && m_queue->Busy();
    if (R_FAILED(envSetNextLoad(m_nro_path.c_str(), m_nro_path.c_str()))) {
        m_keep_queue = false;
        Notice(TrM().nro_sd_only);
        return;
    }
    // its name a moment, as a game that opens, then NXDock goes
    char text[300];
    std::snprintf(text, sizeof(text), TrM().nro_opening_fmt, m_nro_name.c_str());
    m_launch_name = m_nro_name;
    m_launch_text = text;
    m_launch_at = m_time;
}

} // namespace nxdock
