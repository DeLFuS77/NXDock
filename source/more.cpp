// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The More tab (3A of the visual test of phases 9 to 15): a tile for each tool, and the screens of
// installing without the library app (phase 10):
//  - the SD card or a USB disk: a list of files with the places on the left, several can be
//    marked, split files show as one (4A);
//  - from the computer over MTP, and over the USB cable with NS-USBloader: what to do on the
//    computer on the left, what is arriving on the right (5A).
// Everything goes to the usual queue, all or nothing, and the internal memory always asks first.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"
#include "mtp.hpp"
#include "mtp_more.hpp"
#include "usb_disks.hpp"
#include "usb_tinfoil.hpp"

#include <algorithm>
#include <cmath>

namespace nxdock {
namespace {

// Nine tiles: four without the library app, five on the console (the dumps apart from Space, as
// the recommended option A of the visual test of 11/11). kOrder gives the tool of each place.
constexpr int kTiles = 9, kCols = 5, kFirstRow = 4;
constexpr int kOrder[kTiles] = {0, 1, 2, 3, 4, 5, 8, 6, 7};
constexpr float kTileGap = 18, kTileW = (kWidth - 96 - (kCols - 1) * kTileGap) / kCols, kTileH = 170;
constexpr float kSideW = 250;
constexpr float kRowTop = 134, kRowPitch = 50, kRowsBottom = 576;
constexpr int kVisibleRows = int((kRowsBottom - kRowTop) / kRowPitch);

// A small padlock: what NXDock never deletes, renames or moves.
void Padlock(NVGcontext* vg, float x, float cy, NVGcolor c) {
    nvgBeginPath(vg);
    nvgArc(vg, x + 6, cy - 2, 4, 3.1416f, 0, NVG_CW);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, 1.8f);
    nvgStroke(vg);
    ui::FillRound(vg, x + 1, cy - 2, 10, 8, 2, c);
}

std::string BaseName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

// ---- the tiles ----

void App::OpenMoreTile(int place) {
    const auto& s = Tr();
    m_tile = place;
    switch (kOrder[std::clamp(place, 0, kTiles - 1)]) {
        case 0: OpenMoreScreen(MoreScreen::Files); break;
        case 1: OpenMoreScreen(MoreScreen::Mtp); break;
        case 2: OpenMoreScreen(MoreScreen::Usb); break;
        case 3: m_tab = Tab::Shops; break; // the Shops tab
        case 6: OpenMods(); break;
        case 7: AskScreenOff(); break;
        case 5:
            OpenSpace();
            m_space_dumps = false;
            break;
        case 8:
            // the dumps: the game card, or a game installed, to the library
            OpenSpace();
            m_space_dumps = true;
            m_space_focus = 4;
            break;
        case 4:
            OpenVault();
            m_more = MoreScreen::Saves;
            break;
        default: Notice(s.coming_soon); break;
    }
}

void App::OpenMoreScreen(MoreScreen screen) {
    // the computer sends files on its own: the warning of an unsafe system comes first
    if ((screen == MoreScreen::Mtp || screen == MoreScreen::Usb) && !m_risk_ok && m_safety && m_safety->Risky()) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Risk;
        m_after_risk = screen;
        return;
    }
    m_more = screen;
    m_files_nand = false; // the SD card, always, unless chosen for this install
    m_files_after = false; // and the files are kept unless chosen for this install (4A)
    m_files_menu = false;
    switch (screen) {
        case MoreScreen::Files:
            UsbDisksStart();
            m_place = std::clamp(m_place, 0, 8);
            m_places_checked = -100;
            FollowPlaces();
            if (m_folder.empty()) OpenFolder(m_place_roots.empty() ? "sdmc:/" : m_place_roots[0], true);
            break;
        case MoreScreen::Mtp: {
            MtpAllowNand(false);
            // the games installed for their folder (8A): read now if they were not
            if (!m_space) m_space = std::make_unique<Space>();
            if (!m_space->Snapshot().scanned && !m_space->Busy()) m_space->StartScan();
            Space* space = m_space.get();
            MtpSetInstalled([space] { return space->Snapshot().apps; });
            UsbDisksStart();
            const bool ok = MtpStart([this](InstallJob job) { return m_queue && m_queue->Add(std::move(job)); },
                                     [this] { return m_queue && m_queue->Busy(); }, m_ui.mtp_units);
            m_more_error = ok ? std::string{} : std::string(TrM().mtp_failed);
            break;
        }
        case MoreScreen::Usb: {
            auto& usb = UsbLink();
            const bool ok = usb.Start();
            usb.Listen(ok);
            m_more_error = ok ? std::string{} : std::string(TrM().usb_failed);
            break;
        }
        default: break;
    }
}

void App::LeaveMoreScreen() {
    switch (m_more) {
        case MoreScreen::Mtp:
            MtpStop();
            break;
        case MoreScreen::Usb:
            if (m_usb_active) {
                Notice(TrM().wait_usb);
                return;
            }
            UsbLink().Listen(false);
            UsbLink().Stop();
            break;
        default: break;
    }
    m_more = MoreScreen::Tiles;
}

void App::DrawMore(NVGcontext* vg) {
    // a screen of More shown anew (or the tab just opened): its entrance starts
    if (int(m_more) != m_more_seen || m_tab_at > m_more_at) {
        m_more_seen = int(m_more);
        m_more_at = m_list_at = m_time;
        m_glide_list = {};
        m_glide_side = {};
    }
    switch (m_more) {
        case MoreScreen::Tiles: DrawTiles(vg); break;
        case MoreScreen::Files: DrawFiles(vg); break;
        case MoreScreen::Mtp:
        case MoreScreen::Usb: DrawArrival(vg); break;
        case MoreScreen::Saves: DrawVault(vg); break;
        case MoreScreen::Space: DrawSpace(vg); break;
        case MoreScreen::Shops: DrawShops(vg); break;
        case MoreScreen::Mods: DrawMods(vg); break;
        default: DrawTiles(vg); break;
    }
}

void App::UpdateMore(u64 down, u64 held) {
    switch (m_more) {
        case MoreScreen::Tiles: UpdateTiles(down, held); break;
        case MoreScreen::Files: UpdateFiles(down, held); break;
        case MoreScreen::Mtp:
        case MoreScreen::Usb: UpdateArrival(down, held); break;
        case MoreScreen::Saves: UpdateVault(down, held); break;
        case MoreScreen::Space: UpdateSpace(down, held); break;
        case MoreScreen::Shops: UpdateShops(down, held); break;
        case MoreScreen::Mods: UpdateMods(down, held); break;
        default: m_more = MoreScreen::Tiles; break;
    }
}

void App::DrawTiles(NVGcontext* vg) {
    const Theme& t = m_theme;
    const More& m = TrM();
    const char* titles[kTiles] = {m.t_files, m.t_mtp, m.t_usb, m.t_shops, m.t_saves, m.t_space, m.t_mods, m.t_screen, m.t_dumps};
    const char* subs[kTiles] = {m.t_files_sub, m.t_mtp_sub, m.t_usb_sub, m.t_shops_sub, m.t_saves_sub, m.t_space_sub, m.t_mods_sub, m.t_screen_sub, m.t_dumps_sub};
    m_tile = std::clamp(m_tile, 0, kTiles - 1);
    m_tile_lift.resize(kTiles, 0.f);
    for (int i = 0; i < kTiles; i++) {
        const int row = i < kFirstRow ? 0 : 1, col = i < kFirstRow ? i : i - kFirstRow;
        const int tool = kOrder[i];
        const float y0 = 100 + row * (kTileH + 64);
        if (col == 0) draw::Section(vg, t, 50, y0, row == 0 ? m.sect_noserver : m.sect_console);
        const float x = 48 + col * (kTileW + kTileGap), y = y0 + 20;
        AddHit(x, y, kTileW, kTileH, HitKind::MoreTile, i);
        // in one after another with a little bounce (2A); the one in focus lifted and leaning
        const float in = ui::EaseOutBack(ui::Appear(m_more_at + 0.05 * i, 0.45f));
        float& lift = m_tile_lift[size_t(i)];
        const float want = i == m_tile ? 1.f : 0.f;
        lift = ui::Still() ? want : lift + (want - lift) * 0.25f;
        const float cx = x + kTileW / 2, cy = y + kTileH / 2;
        nvgSave(vg);
        nvgTranslate(vg, cx, cy + (1 - in) * 18 - lift * 6);
        if (ui::Lively()) nvgRotate(vg, -0.012f * lift);
        const float sc = (0.94f + 0.06f * in) * (1 + 0.03f * lift);
        nvgScale(vg, sc, sc);
        nvgTranslate(vg, -cx, -cy);
        nvgGlobalAlpha(vg, std::min(1.f, ui::Appear(m_more_at + 0.05 * i, 0.3f) * 1.2f));
        draw::Panel(vg, t, x, y, kTileW, kTileH, 16);
        // a touch of the colour of its group from the top (13A)
        const NVGcolor group = row == 0 ? Hex(0x2a8fe0) : Hex(0x6a4ae0);
        NVGcolor wash = group;
        wash.a = t.dark ? 0.10f : 0.06f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, kTileW, kTileH, 16);
        nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + kTileH, wash, Hex(0, 0)));
        nvgFill(vg);
        if (i == m_tile) {
            ui::Focus(vg, t, x, y, kTileW, kTileH, 16, m_time);
            if (ui::Lively()) {
                // a gleam crossing it now and then
                const float p = float(std::fmod(m_time * 0.45, 1.6)) - 0.3f;
                const float gx = x + p * kTileW;
                nvgSave(vg);
                nvgIntersectScissor(vg, x, y, kTileW, kTileH);
                nvgBeginPath(vg);
                nvgRect(vg, x, y, kTileW, kTileH);
                nvgFillPaint(vg, nvgLinearGradient(vg, gx - 60, y, gx + 60, y + kTileH * 0.4f, nvgRGBAf(1, 1, 1, 0), nvgRGBAf(1, 1, 1, 0)));
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, gx - 30, y);
                nvgLineTo(vg, gx + 10, y);
                nvgLineTo(vg, gx - 50, y + kTileH);
                nvgLineTo(vg, gx - 90, y + kTileH);
                nvgClosePath(vg);
                nvgFillColor(vg, nvgRGBAf(1, 1, 1, t.dark ? 0.07f : 0.18f));
                nvgFill(vg);
                nvgRestore(vg);
            }
        }
        // its picture in white in a bubble of the colour of its group: blue to install without the
        // library, violet for the console (13A); never red
        const float bx = x + 20, by = y + 18, bs = 52;
        nvgBeginPath(vg);
        nvgRect(vg, bx - 14, by - 6, bs + 28, bs + 26);
        nvgFillPaint(vg, nvgBoxGradient(vg, bx, by + 5, bs, bs, 15, 12, nvgRGBAf(0, 0, 0, t.dark ? 0.4f : 0.18f), nvgRGBAf(0, 0, 0, 0)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, bx, by, bs, bs, 15);
        nvgFillPaint(vg, row == 0 ? nvgLinearGradient(vg, bx, by, bx + bs, by + bs, Hex(0x4fc8f5), Hex(0x2a6fe0))
                                  : nvgLinearGradient(vg, bx, by, bx + bs, by + bs, Hex(0xa58cff), Hex(0x5a3fd0)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, bx + 12, by + 1.2f);
        nvgLineTo(vg, bx + bs - 12, by + 1.2f);
        nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, 0.35f));
        nvgStrokeWidth(vg, 1.2f);
        nvgStroke(vg);
        const NVGcolor white = nvgRGB(255, 255, 255);
        nvgSave(vg);
        nvgTranslate(vg, bx + bs / 2, by + bs / 2);
        nvgScale(vg, 0.8f, 0.8f);
        nvgTranslate(vg, -(bx + bs / 2), -(by + bs / 2));
        DrawTileIcon(vg, tool, bx + bs / 2 - 16, by + bs / 2, &white);
        nvgRestore(vg);
        nvgSave(vg);
        nvgIntersectScissor(vg, x + 12, y, kTileW - 24, kTileH);
        ui::Title(vg, x + 20, y + 94, 19, t.text, titles[tool]);
        nvgRestore(vg);
        draw::Box(vg, x + 20, y + 112, kTileW - 40, 14, t.muted, subs[tool]);
        nvgRestore(vg);
    }
    std::vector<ui::Hint> hints = {{ui::glyph::A, m.open, HidNpadButton_A}, {ui::glyph::Plus, Tr().exit, HidNpadButton_Plus}};
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

// The pictures of the tiles, drawn in the blue of work: an SD card, a screen, a plug, a shop, a
// safe, a pie, a wand and a moon.
void App::DrawTileIcon(NVGcontext* vg, int tile, float x, float cy, const NVGcolor* ink) {
    const Theme& t = m_theme;
    const NVGcolor c = ink ? *ink : t.work;
    // each picture moves a little in its own way (2A)
    nvgSave(vg);
    if (ui::Lively()) {
        const float tt = float(m_time) + float(tile) * 0.7f;
        const float px = x + 16, py = cy;
        switch (tile) {
            case 0: nvgTranslate(vg, 0, -3 * std::fabs(std::sin(tt * 1.6f))); break;              // the card hops
            case 2: {                                                                                // the plug beats
                const float k = 1 + 0.08f * std::max(0.f, std::sin(tt * 4.f));
                nvgTranslate(vg, px, py);
                nvgScale(vg, k, k);
                nvgTranslate(vg, -px, -py);
                break;
            }
            case 3:                                                                                  // the shop sways
                nvgTranslate(vg, px, py - 16);
                nvgRotate(vg, 0.07f * std::sin(tt * 1.5f));
                nvgTranslate(vg, -px, -(py - 16));
                break;
            case 5:
            case 4: {                                                                                // pie and safe breathe
                const float k = 1 + 0.05f * std::sin(tt * 2.f);
                nvgTranslate(vg, px, py);
                nvgScale(vg, k, k);
                nvgTranslate(vg, -px, -py);
                break;
            }
            case 6:                                                                                  // the wand waves
                nvgTranslate(vg, x + 2, cy + 14);
                nvgRotate(vg, -0.22f * (0.5f + 0.5f * std::sin(tt * 3.f)));
                nvgTranslate(vg, -(x + 2), -(cy + 14));
                break;
            default: break;
        }
        if (tile == 1 && std::fmod(tt, 1.8f) > 1.2f) nvgGlobalAlpha(vg, 0.55f); // the screen blinks
        if (tile == 7) {                                                         // the moon glows
            NVGcolor glow = c;
            glow.a = 0.25f + 0.2f * std::sin(tt * 2.f);
            NVGcolor clear = c;
            clear.a = 0;
            nvgBeginPath(vg);
            nvgCircle(vg, x + 16, cy, 26);
            nvgFillPaint(vg, nvgRadialGradient(vg, x + 16, cy, 8, 26, glow, clear));
            nvgFill(vg);
        }
    }
    nvgStrokeColor(vg, c);
    nvgFillColor(vg, c);
    nvgStrokeWidth(vg, 2.6f);
    switch (tile) {
        case 0: // an SD card
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 6, cy - 16);
            nvgLineTo(vg, x + 26, cy - 16);
            nvgLineTo(vg, x + 26, cy + 16);
            nvgLineTo(vg, x, cy + 16);
            nvgLineTo(vg, x, cy - 10);
            nvgClosePath(vg);
            nvgStroke(vg);
            for (int k = 0; k < 3; k++) ui::FillRound(vg, x + 8 + k * 5.5f, cy - 12, 3, 7, 1, c);
            break;
        case 1: // a computer screen
            ui::StrokeRound(vg, x - 2, cy - 15, 36, 24, 3, c, 2.6f);
            ui::FillRound(vg, x + 10, cy + 12, 12, 3, 1, c);
            ui::FillRound(vg, x + 6, cy + 16, 20, 2.6f, 1, c);
            break;
        case 2: // a plug
            ui::StrokeRound(vg, x + 6, cy - 6, 20, 16, 3, c, 2.6f);
            ui::FillRound(vg, x + 10, cy - 15, 3, 9, 1, c);
            ui::FillRound(vg, x + 19, cy - 15, 3, 9, 1, c);
            ui::FillRound(vg, x + 14.5f, cy + 10, 3, 8, 1, c);
            break;
        case 3: // a shop with an awning
            nvgBeginPath(vg);
            nvgMoveTo(vg, x, cy - 6);
            nvgLineTo(vg, x + 4, cy - 15);
            nvgLineTo(vg, x + 30, cy - 15);
            nvgLineTo(vg, x + 34, cy - 6);
            nvgClosePath(vg);
            nvgStroke(vg);
            ui::StrokeRound(vg, x + 3, cy - 6, 28, 22, 2, c, 2.6f);
            ui::FillRound(vg, x + 13, cy + 4, 8, 12, 1, c);
            break;
        case 4: // a safe
            ui::StrokeRound(vg, x, cy - 15, 32, 30, 4, c, 2.6f);
            nvgBeginPath(vg);
            nvgCircle(vg, x + 16, cy, 7);
            nvgStroke(vg);
            ui::FillRound(vg, x + 15, cy - 3, 2, 6, 1, c);
            break;
        case 5: // a pie of space
            nvgBeginPath(vg);
            nvgCircle(vg, x + 16, cy, 15);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 16, cy);
            nvgArc(vg, x + 16, cy, 15, -1.5708f, 0.6f, NVG_CW);
            nvgClosePath(vg);
            nvgFill(vg);
            break;
        case 8: // a tray with an arrow into it
            ui::StrokeRound(vg, x, cy + 2, 32, 14, 3, c, 2.6f);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 16, cy - 16);
            nvgLineTo(vg, x + 16, cy + 6);
            nvgMoveTo(vg, x + 9, cy - 1);
            nvgLineTo(vg, x + 16, cy + 6);
            nvgLineTo(vg, x + 23, cy - 1);
            nvgStroke(vg);
            break;
        case 6: // a wand with a sparkle
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 2, cy + 14);
            nvgLineTo(vg, x + 22, cy - 6);
            nvgStroke(vg);
            for (int k = 0; k < 4; k++) {
                const float a = float(k) * 1.5708f;
                nvgBeginPath(vg);
                nvgMoveTo(vg, x + 26, cy - 10);
                nvgLineTo(vg, x + 26 + std::cos(a) * 8, cy - 10 + std::sin(a) * 8);
                nvgStroke(vg);
            }
            break;
        default: // a moon
            nvgBeginPath(vg);
            nvgCircle(vg, x + 16, cy, 15);
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgCircle(vg, x + 24, cy - 6, 13);
            nvgFillColor(vg, ink ? Hex(0x8a70f0) : t.panel); // in its bubble, the violet behind it
            nvgFill(vg);
            break;
    }
    nvgRestore(vg);
}

void App::UpdateTiles(u64 down, u64 held) {
    // four in the first row, five in the second
    const bool second = m_tile >= kFirstRow;
    const int col = second ? m_tile - kFirstRow : m_tile;
    if (Repeat(down, held, HidNpadButton_AnyLeft) && col > 0) m_tile--;
    if (Repeat(down, held, HidNpadButton_AnyRight) && col < (second ? kTiles - kFirstRow : kFirstRow) - 1) m_tile++;
    if (Repeat(down, held, HidNpadButton_AnyUp) && second) m_tile = std::min(col, kFirstRow - 1);
    if (Repeat(down, held, HidNpadButton_AnyDown) && !second) m_tile = kFirstRow + col;
    if (down & HidNpadButton_A) OpenMoreTile(m_tile);
}

// ---- the SD card and USB disks (4A) ----

void App::FollowPlaces() {
    if (m_time - m_places_checked < 2) return;
    m_places_checked = m_time;
    const More& m = TrM();
    std::vector<std::string> roots = {"sdmc:/"}, names = {Tr().sd_card};
    for (const auto& d : UsbDisks()) {
        roots.push_back(d.mount);
        names.push_back(std::string(m.usb_disk) + (d.label.empty() ? "" : " · " + d.label));
    }
    if (roots != m_place_roots) {
        const std::string current = m_place < int(m_place_roots.size()) ? m_place_roots[m_place] : "";
        m_place_roots = roots;
        m_place_names = names;
        // the disk shown was pulled out: back to the SD card
        const auto it = std::find(roots.begin(), roots.end(), current);
        if (it == roots.end() && !current.empty()) {
            m_place = 0;
            OpenFolder(roots[0], true);
        } else if (it != roots.end()) {
            m_place = int(it - roots.begin());
        }
    }
}

void App::OpenFolder(const std::string& folder, bool root) {
    m_folder = folder;
    m_folder_root = root;
    m_entries = ListFolder(folder, root, m_files_all);
    m_marked.assign(m_entries.size(), 0);
    m_mark_anchor = -1;
    m_file_focus = 0;
    m_file_first = 0;
    m_list_at = m_time;
}

void App::InstallMarkedFiles() {
    const auto& s = Tr();
    const More& m = TrM();
    std::vector<InstallJob> jobs;
    bool any = std::any_of(m_marked.begin(), m_marked.end(), [](char c) { return c; });
    std::vector<fileops::GameFile> files;
    for (size_t i = 0; i < m_entries.size(); i++) {
        const auto& e = m_entries[i];
        const bool chosen = any ? m_marked[i] : int(i) == m_file_focus;
        if (!chosen || e.name == "..") continue;
        if (e.dir) {
            // a folder marked: the games it holds (1A)
            if (any) {
                const auto inside = fileops::GamesUnder({e.path});
                files.insert(files.end(), inside.begin(), inside.end());
            }
            continue;
        }
        if (e.format.empty()) continue; // not a game
        files.push_back({e.name, e.parts, e.size, e.format});
    }
    for (const auto& f : files) {
        InstallJob job;
        job.game = f.name;
        job.label = f.parts.size() > 1 ? std::string(m.t_files) + " · " + std::to_string(f.parts.size()) : m.t_files;
        job.format = f.format;
        job.origin = InstallJob::Origin::File;
        job.resumable = true; // a journal while it installs (23A)
        job.parts = f.parts;
        job.file_id = "file:" + f.parts.front();
        job.size = f.size;
        job.nand = m_files_nand;
        job.color_key = f.name;
        job.delete_after = m_files_after; // 4A
        jobs.push_back(std::move(job));
    }
    if (jobs.empty()) {
        Notice(s.nothing_picked);
        return;
    }
    std::fill(m_marked.begin(), m_marked.end(), 0);
    QueueLocal(std::move(jobs));
    m_files_nand = false; // the next ones go to the SD card again
}

// Jobs that do not come from the library: the internal memory asks first, as on a game's page.
void App::QueueLocal(std::vector<InstallJob> jobs) {
    // in the order they go on: each game, then its update, then its DLC (an update is merged with
    // a game NXDock merged, and the console asks for the game before its DLC)
    const auto rank = [](const InstallJob& j) {
        u64 id = 0;
        u32 v = 0;
        if (!TitleFromName(j.game + " " + j.label, id, v)) return std::pair<u64, int>{~0ull, 0};
        const int kind = TitleKind(id);
        const u64 app = kind == 0 ? id : kind == 1 ? id & ~u64(0xFFF) : (id - 0x1000) & ~u64(0xFFF);
        return std::pair<u64, int>{app, kind};
    };
    std::stable_sort(jobs.begin(), jobs.end(), [&](const InstallJob& a, const InstallJob& b) { return rank(a) < rank(b); });
    if (!jobs.empty() && jobs.front().nand) {
        m_sd_checked = -100;
        CheckSdSpace();
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Nand;
        m_dialog.jobs = std::move(jobs);
        return;
    }
    AddJobs(std::move(jobs));
}

void App::DrawFiles(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    FollowPlaces();
    CheckSdSpace();

    // the places on the left
    nvgBeginPath(vg);
    nvgRect(vg, kSideW, 73, 1, 664 - 73);
    nvgFillColor(vg, t.line);
    nvgFill(vg);
    draw::Section(vg, t, 28, 100, m.places);
    {
        // the chosen place glides there
        NVGcolor bg = t.work;
        bg.a = 0.16f;
        const float py = m_glide_side.Step(118 + m_place * 50.f);
        ui::FillRound(vg, 14, py, kSideW - 28, 42, 10, bg);
        if (m_files_side) ui::Focus(vg, t, 14, py, kSideW - 28, 42, 10, m_time);
    }
    for (int i = 0; i < int(m_place_roots.size()); i++) {
        const float y = 118 + i * 50;
        const bool on = i == m_place;
        AddHit(14, y - 3, kSideW - 28, 48, HitKind::MorePlace, i); // 48 px for a finger
        nvgSave(vg);
        nvgScissor(vg, 24, y, kSideW - 48, 42);
        ui::Text(vg, 28, y + 21, 16, on ? t.text : t.muted, m_place_names[i].c_str());
        nvgRestore(vg);
    }
    if (m_place_roots.size() < 2) {
        draw::Box(vg, 28, 118 + 60, kSideW - 56, 14, t.muted, m.no_usb);
    }

    // where we are, and the list
    const float lx = kSideW + 24, lw = kWidth - kSideW - 48;
    const Tools& tt = TrT();
    // the view on the right: the games, or every file (1A)
    float vx = lx + lw;
    {
        const char* names[2] = {tt.fv_games, tt.fv_all};
        float widths[2], all = 0;
        for (int k = 0; k < 2; k++) all += widths[k] = ui::TextWidth(vg, 14, names[k]) + 28;
        vx -= all;
        ui::StrokeRound(vg, vx, 89, all, 30, 15, t.line, 1);
        float cx = vx;
        for (int k = 0; k < 2; k++) {
            const bool on = (k == 1) == m_files_all;
            if (on) ui::FillRound(vg, cx, 89, widths[k], 30, 15, t.work);
            ui::Text(vg, cx + widths[k] / 2, 104, 14, on ? t.on_work : t.muted, names[k], NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            AddHit(cx, 80, widths[k], 48, HitKind::FilesView, k);
            cx += widths[k];
        }
    }
    // what was copied or cut, to paste
    if (!m_clip.empty()) {
        char text[300];
        std::snprintf(text, sizeof(text), m_clip_move ? tt.fc_move_fmt : tt.fc_copy_fmt, int(m_clip.size()), draw::Size(m_clip_bytes).c_str(),
                      m_clip_from.c_str());
        const float cw = std::min(ui::TextWidth(vg, 14, text) + 28, lw * 0.5f);
        vx -= cw + 10;
        NVGcolor bg = t.work;
        bg.a = 0.16f;
        ui::FillRound(vg, vx, 89, cw, 30, 15, bg);
        nvgSave(vg);
        nvgIntersectScissor(vg, vx + 12, 89, cw - 24, 30);
        ui::Text(vg, vx + 14, 104, 14, t.text, text);
        nvgRestore(vg);
    }
    nvgSave(vg);
    nvgScissor(vg, lx, 86, vx - lx - 12, 30);
    ui::Text(vg, lx + 4, 104, 15, t.muted, m_folder.c_str());
    nvgRestore(vg);
    if (m_entries.empty()) {
        ui::Text(vg, lx + lw / 2, 300, 18, t.muted, m.empty_folder, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    m_file_focus = std::clamp(m_file_focus, 0, std::max(0, int(m_entries.size()) - 1));
    if (m_file_focus < m_file_first) m_file_first = m_file_focus;
    if (m_file_focus >= m_file_first + kVisibleRows) m_file_first = m_file_focus - kVisibleRows + 1;
    if (!m_entries.empty()) {
        // the focus glides from row to row (3A)
        const float fy = m_glide_list.Step(kRowTop + (m_file_focus - m_file_first) * kRowPitch);
        ui::FillRound(vg, lx, fy, lw, 44, 10, t.panel2);
        if (!m_files_side) ui::Focus(vg, t, lx, fy, lw, 44, 10, m_time);
    }
    ListArea(lx, kRowTop, lw, kVisibleRows * kRowPitch, kRowPitch);
    for (int r = 0; r < kVisibleRows && m_file_first + r < int(m_entries.size()); r++) {
        const int i = m_file_first + r;
        const auto& e = m_entries[i];
        const float y = kRowTop + r * kRowPitch, cy = y + 22;
        // the rows come in one after another
        const float in = ui::EaseOut(ui::Appear(m_list_at + 0.04 * r, 0.3f));
        nvgSave(vg);
        nvgTranslate(vg, -(1 - in) * 24, 0);
        nvgGlobalAlpha(vg, in);
        AddHit(lx + 50, y - 2, lw - 50, 48, HitKind::MoreRow, i);
        if (e.name != "..") {
            // files and folders can be marked (1A)
            draw::TickBox(vg, t, lx + 14, cy, m_marked[i], "file:" + e.path);
            AddHit(lx, y - 2, 50, 48, HitKind::MoreMark, i);
        }
        if (e.dir) draw::FolderIcon(vg, t, lx + 54, cy);
        else draw::FileIcon(vg, t, lx + 54, cy);
        std::string name = e.name;
        if (e.parts.size() > 1) {
            char split[48];
            std::snprintf(split, sizeof(split), m.split_fmt, int(e.parts.size()));
            name += "  (" + std::string(split) + ")";
        }
        u64 tid = 0;
        u32 tver = 0;
        const bool titled = !e.dir && TitleFromName(e.name, tid, tver);
        nvgSave(vg);
        nvgScissor(vg, lx + 90, y, lw - 90 - (titled ? 340 : 220), 44);
        const float nx = ui::Text(vg, lx + 90, cy, 16, e.dir || !e.format.empty() ? t.text : t.muted, name.c_str());
        nvgRestore(vg);
        if (e.name != "..") {
            const auto guard = fileops::GuardOf(e.path);
            if (guard == fileops::Guard::System || guard == fileops::Guard::Self) {
                Padlock(vg, std::min(nx + 10, lx + lw - (titled ? 340 : 220)), cy, t.muted);
            }
        }
        if (titled) {
            // what it is, as the numbers alone do not say it
            const int kind = TitleKind(tid);
            const char* tag = kind == 0 ? m.game_word : kind == 1 ? m.ic_update_word : m.ic_dlc_word;
            const NVGcolor col = kind == 0 ? t.work : kind == 1 ? t.cyan : t.violet;
            const float tw = ui::TextWidth(vg, 12, tag) + 14, tx = lx + lw - 170 - tw;
            ui::StrokeRound(vg, tx, cy - 9, tw, 18, 9, col, 1.f);
            ui::Text(vg, tx + tw / 2, cy, 12, col, tag, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        if (!e.dir) {
            std::string fmt = e.format;
            if (fmt.empty()) {
                // a file that is not a game: its extension
                const auto dot = e.name.find_last_of('.');
                if (dot != std::string::npos && dot + 1 < e.name.size() && e.name.size() - dot <= 6) fmt = e.name.substr(dot + 1);
            }
            for (auto& c : fmt) c = char(std::toupper((unsigned char)c));
            ui::Text(vg, lx + lw - 120, cy, 15, t.muted, fmt.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            ui::Text(vg, lx + lw - 14, cy, 15, t.muted, draw::Size(e.size).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        nvgRestore(vg);
    }

    // what is marked, where it goes, and what is left after
    int count = 0;
    unsigned long long bytes = 0;
    for (size_t i = 0; i < m_entries.size(); i++) {
        if (m_marked[i]) {
            count++;
            bytes += m_entries[i].size;
        }
    }
    // what is marked, and where it goes in large: the SD card unless chosen for this install
    const float sy = 586, sh = 64;
    char text[200];
    std::snprintf(text, sizeof(text), m.marked_short_fmt, count, draw::Size(bytes).c_str());
    const float mw = std::max({190.f, ui::TextWidth(vg, 16, text) + 40, ui::TextWidth(vg, 13, tt.fa_switch) + 72});
    draw::Panel(vg, t, lx, sy, mw, sh, 12);
    ui::CheckMark(vg, lx + 22, sy + 21, 12, t.work, 2.4f);
    ui::Text(vg, lx + 40, sy + 21, 16, t.text, text);
    draw::Toggle(vg, t, lx + 16, sy + 45, m_files_after, "files-after");
    ui::Text(vg, lx + 66, sy + 45, 13, m_files_after ? t.text : t.muted, tt.fa_switch);
    AddHit(lx, sy, mw, sh, HitKind::FilesAfter);
    DestCards(vg, lx + mw + 12, sy, lw - mw - 12, sh, m_files_nand, bytes, HitKind::FilesDest);

    std::vector<ui::Hint> hints = {{ui::glyph::Y, m.mark, HidNpadButton_Y},
                                   {ui::glyph::ZL, tt.fm_all, HidNpadButton_ZL},
                                   {ui::glyph::Minus, tt.fm_actions, HidNpadButton_Minus},
                                   {ui::glyph::X, m.install_marked, HidNpadButton_X},
                                   {ui::glyph::ZR, m.dc_switch, HidNpadButton_ZR},
                                   {ui::glyph::B, s.back, HidNpadButton_B}};
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
    if (m_files_menu) DrawFilesMenu(vg);
}

void App::UpdateFiles(u64 down, u64 held) {
    if (m_files_menu) {
        UpdateFilesMenu(down, held);
        return;
    }
    if (!(held & HidNpadButton_Y)) m_mark_anchor = -1; // the range ends when Y is let go
    if (Repeat(down, held, HidNpadButton_AnyLeft)) m_files_side = true;
    if (Repeat(down, held, HidNpadButton_AnyRight)) m_files_side = false;
    if (down & HidNpadButton_ZR) m_files_nand = !m_files_nand;
    if (m_files_side) {
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_place > 0) m_place--;
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_place < int(m_place_roots.size()) - 1) m_place++;
        if ((down & HidNpadButton_A) && m_place < int(m_place_roots.size())) {
            OpenFolder(m_place_roots[m_place], true);
            m_files_side = false;
        }
        if (down & HidNpadButton_B) LeaveMoreScreen();
        return;
    }
    const int before = m_file_focus;
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_file_focus > 0) m_file_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_file_focus < int(m_entries.size()) - 1) m_file_focus++;
    // Y held while moving marks the rows in between, as the first one was set (1A)
    if (held & HidNpadButton_Y) FilesMarkMove(before, m_file_focus);
    const bool has = m_file_focus >= 0 && m_file_focus < int(m_entries.size());
    if (has && (down & (HidNpadButton_A | HidNpadButton_Y))) {
        const auto e = m_entries[m_file_focus];
        if (e.dir && (down & HidNpadButton_A)) {
            const bool to_root = e.name == ".." && std::find(m_place_roots.begin(), m_place_roots.end(), e.path) != m_place_roots.end();
            OpenFolder(e.path, to_root);
        } else if (e.name != "..") {
            m_marked[m_file_focus] = !m_marked[m_file_focus];
            if (down & HidNpadButton_Y) {
                m_mark_anchor = m_file_focus;
                m_mark_value = m_marked[m_file_focus];
            }
        }
    }
    if (down & HidNpadButton_ZL) {
        // all, or none when all are marked
        bool all = true;
        for (size_t i = 0; i < m_entries.size(); i++) all = all && (m_entries[i].name == ".." || m_marked[i]);
        for (size_t i = 0; i < m_entries.size(); i++) m_marked[i] = !all && m_entries[i].name != "..";
    }
    if ((down & HidNpadButton_Minus) && !m_fwork) OpenFilesMenu();
    if (down & HidNpadButton_X) InstallMarkedFiles();
    if (down & HidNpadButton_B) {
        // up a folder, then back to the tiles
        if (!m_folder_root && !m_entries.empty() && m_entries[0].name == "..") {
            const auto up = m_entries[0].path;
            OpenFolder(up, std::find(m_place_roots.begin(), m_place_roots.end(), up) != m_place_roots.end());
        } else {
            LeaveMoreScreen();
        }
    }
}

// ---- MTP and NS-USBloader (5A) ----

void App::FollowUsb() {
    auto& usb = UsbLink();
    if (!usb.Started() || !m_queue) return;
    std::vector<std::string> names;
    if (!m_usb_active && usb.TakeList(names)) {
        std::vector<InstallJob> jobs;
        for (const auto& n : names) {
            const std::string fmt = GameFormat(n);
            if (fmt.empty()) continue;
            InstallJob job;
            job.game = BaseName(n);
            job.label = TrM().usb_from;
            job.format = fmt;
            job.origin = InstallJob::Origin::Usb;
            job.parts = {n};
            job.file_id = "usb:" + n;
            job.nand = m_files_nand;
            job.color_key = job.game;
            jobs.push_back(std::move(job));
        }
        if (jobs.empty()) {
            usb.SayDone();
            return;
        }
        m_usb_active = true;
        QueueLocal(std::move(jobs));
        m_files_nand = false; // the next ones go to the SD card again
        return;
    }
    if (m_usb_active && m_dialog.kind == DialogKind::None) {
        // what came over the cable is installed (or was cancelled): the computer is told
        const auto jobs = m_queue->Jobs();
        const bool pending = std::any_of(jobs.begin(), jobs.end(), [](const InstallJob& j) {
            return j.origin == InstallJob::Origin::Usb && (j.state == InstallJob::State::Waiting || j.state == InstallJob::State::Running);
        });
        if (!pending) {
            usb.SayDone();
            m_usb_active = false;
        }
    }
}

void App::DrawArrival(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const bool mtp = m_more == MoreScreen::Mtp;
    const MtpView mv = mtp ? MtpSnapshot() : MtpView{};
    const bool connected = mtp ? mv.connected : UsbLink().Connected();

    const float px = 48, py = 98, pw = (kWidth - 96 - 36) / 2, ph = 540;
    // what to do on the computer
    draw::Panel(vg, t, px, py, pw, ph, 16);
    ui::Text(vg, px + 26, py + 36, 22, t.text, mtp ? m.mtp_title : m.usb_title);
    float y = py + 64;
    if (mtp) {
        y += draw::Box(vg, px + 26, y, pw - 52, 16, t.muted, m.mtp_hint) + 18;
        struct Folder { const char* name; const char* sub; bool off; };
        const Folder folders[] = {{m.mtp_install_sd, m.mtp_install_sd_sub, false},
                                  {m.mtp_install_nand, mv.nand_allowed ? m.nand_allowed : m.mtp_install_nand_sub, !mv.nand_allowed},
                                  {m.mtp_sd_card, m.mtp_sd_card_sub, false}};
        for (const auto& f : folders) {
            ui::FillRound(vg, px + 26, y, pw - 52, 58, 12, t.panel2);
            ui::StrokeRound(vg, px + 26.5f, y + .5f, pw - 53, 57, 12, t.line, 1);
            nvgGlobalAlpha(vg, f.off ? 0.55f : 1.f);
            draw::FolderIcon(vg, t, px + 44, y + 29);
            ui::Text(vg, px + 82, y + 19, 16, t.text, f.name);
            ui::Text(vg, px + 82, y + 41, 13, t.muted, f.sub);
            nvgGlobalAlpha(vg, 1.f);
            y += 64;
        }
        // the read-only folders, each with its switch (8A)
        const Tools& tt = TrT();
        ui::Text(vg, px + 28, y + 12, 13, t.muted, tt.mu_title);
        y += 24;
        const bool disk = !UsbDisks().empty();
        const struct { const char* name; const char* sub; } units[] = {
            {tt.mu_installed, tt.mu_installed_sub}, {tt.mu_saves, tt.mu_saves_sub}, {tt.mu_album, tt.mu_album_sub},
            {tt.mu_usb, disk ? tt.mu_usb_sub : tt.mu_usb_none}};
        for (int k = 0; k < 4; k++) {
            const bool on = (m_ui.mtp_units >> k) & 1;
            if (k == m_mtp_unit) {
                ui::FillRound(vg, px + 26, y, pw - 52, 46, 10, t.panel2);
                ui::Focus(vg, t, px + 26, y, pw - 52, 46, 10, m_time);
            }
            AddHit(px + 26, y - 1, pw - 52, 48, HitKind::MtpUnit, k);
            nvgSave(vg);
            nvgScissor(vg, px + 40, y, pw - 52 - 90, 46);
            const float nx = ui::Text(vg, px + 44, y + 23, 15, on ? t.text : t.muted, units[k].name) + 10;
            ui::Text(vg, nx, y + 23, 13, t.muted, units[k].sub);
            nvgRestore(vg);
            draw::Toggle(vg, t, px + pw - 26 - 62, y + 23, on, "mtp-unit:" + std::to_string(k));
            y += 50;
        }
    } else {
        draw::Box(vg, px + 26, y, pw - 52, 17, t.muted, m.usb_steps);
        // where what arrives goes, in large: the SD card unless chosen
        DestCards(vg, px + 20, py + ph - 84, pw - 40, 64, m_files_nand, 0, HitKind::FilesDest);
    }
    if (!m_more_error.empty()) {
        draw::Box(vg, px + 26, py + ph - (mtp ? 70 : 150), pw - 52, 15, t.err, m_more_error.c_str());
    }

    // what is arriving
    const float rx = px + pw + 36;
    draw::Panel(vg, t, rx, py, pw, ph, 16);
    ui::Text(vg, rx + 26, py + 36, 22, t.text, m.arriving);
    ui::Text(vg, rx + 26, py + 66, 15, connected ? t.ok : t.muted, connected ? m.connected_usb : m.not_connected);
    // the job of the queue that comes from here, if any
    const auto jobs = m_queue ? m_queue->Jobs() : std::vector<InstallJob>{};
    const auto origin = mtp ? InstallJob::Origin::Mtp : InstallJob::Origin::Usb;
    const InstallJob* current = nullptr;
    int done = 0, failed = 0;
    for (const auto& j : jobs) {
        if (j.origin != origin) continue;
        if (j.state == InstallJob::State::Running) current = &j;
        if (j.state == InstallJob::State::Done) done++;
        if (j.state == InstallJob::State::Failed) failed++;
    }
    float cy = py + 96;
    if (current) {
        const auto live = m_queue->GetLive();
        ui::FillRound(vg, rx + 26, cy, pw - 52, 96, 12, t.panel2);
        ui::Cover(vg, t, rx + 40, cy + 16, 64, PlaceholderColor(current->color_key), Initial(current->game).c_str(), nullptr, false, false, 9, 0);
        nvgSave(vg);
        nvgScissor(vg, rx + 118, cy + 8, pw - 52 - 200, 40);
        ui::Text(vg, rx + 118, cy + 30, 17, t.text, current->game.c_str());
        nvgRestore(vg);
        float frac = live.total ? float(double(live.written) / double(live.total)) : 0.f;
        if (mtp && mv.size) frac = float(double(mv.received) / double(mv.size));
        char line[96];
        std::snprintf(line, sizeof(line), "%s · %.0f MB/s", current->nand ? s.internal : s.sd_card, mtp ? live.net_rate : live.write_rate);
        ui::Text(vg, rx + 118, cy + 54, 14, t.muted, line);
        ui::Progress(vg, t, rx + 118, cy + 70, pw - 52 - 200, 10, std::clamp(frac, 0.f, 1.f), t.work, m_time);
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%d %%", int(std::clamp(frac, 0.f, 1.f) * 100));
        ui::Text(vg, rx + pw - 40, cy + 48, 20, t.text, pct, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        cy += 112;
    }
    if (connected) {
        ui::Spinner(vg, rx + 36, cy + 16, 8, t.work, m_time);
        ui::Text(vg, rx + 54, cy + 16, 15, t.muted, m.waiting_files);
        cy += 40;
    }
    if (done || failed) {
        char line[96];
        std::snprintf(line, sizeof(line), m.count_fmt, done, failed);
        ui::Text(vg, rx + 26, cy + 10, 15, t.muted, line);
    }
    {
        // the console, with what arrives flying into its screen (its Joy-Con in their own colours)
        const float ccx = rx + pw / 2 + 60, ccy = py + ph - 110, sw = 150, sh = 84, jw = 30;
        const float sx = ccx - sw / 2, sy = ccy - sh / 2;
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, sx - jw, sy, jw, sh, sh / 2.6f, 0, 0, sh / 2.6f);
        nvgFillColor(vg, nvgRGB(0x0a, 0xb9, 0xe6));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, sx + sw, sy, jw, sh, 0, sh / 2.6f, sh / 2.6f, 0);
        nvgFillColor(vg, nvgRGB(0xff, 0x3c, 0x28));
        nvgFill(vg);
        ui::FillRound(vg, sx, sy, sw, sh, 3, nvgRGB(0x2b, 0x2f, 0x36));
        ui::FillRound(vg, sx + 7, sy + 7, sw - 14, sh - 14, 2, nvgRGB(0x11, 0x13, 0x17));
        NVGcolor glow = t.work;
        glow.a = current ? 0.35f + 0.15f * float(std::sin(m_time * 3)) : 0.12f;
        NVGcolor clear = t.work;
        clear.a = 0;
        nvgBeginPath(vg);
        nvgRect(vg, sx + 7, sy + 7, sw - 14, sh - 14);
        nvgFillPaint(vg, nvgRadialGradient(vg, ccx, ccy, 4, sw / 2, glow, clear));
        nvgFill(vg);
        if (current && ui::Lively()) {
            for (int k = 0; k < 3; k++) {
                const float p = float(std::fmod(m_time * 0.45 + k / 3.0, 1.0));
                const float fx = rx + 40 + (ccx - rx - 40) * p, fy = ccy + 30 - 60 * p + 70 * p * (1 - p);
                const float s = 22 * (1 - 0.6f * p * p);
                nvgGlobalAlpha(vg, std::min(1.f, p * 5) * (1 - p * p));
                ui::FillRound(vg, fx - s / 2, fy - s * 0.6f, s, s * 1.2f, 3, t.work);
                ui::FillRound(vg, fx - s / 4, fy - s * 0.2f, s / 2, 2, 1, t.on_work);
                nvgGlobalAlpha(vg, 1.f);
            }
        }
    }

    std::vector<ui::Hint> hints;
    // the internal memory allowed can be turned off again: only the SD card
    if (mtp) hints.push_back({ui::glyph::A, m.st_change, HidNpadButton_A});
    if (mtp) hints.push_back({ui::glyph::X, mv.nand_allowed ? m.mtp_sd_only : m.allow_nand, HidNpadButton_X});
    if (!mtp) hints.push_back({ui::glyph::ZR, m.dc_switch, HidNpadButton_ZR});
    hints.push_back({ui::glyph::B, m.stop_back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::ToggleMtpUnit(int unit) {
    m_ui.mtp_units ^= 1 << std::clamp(unit, 0, 3);
    m_ui.Save();
    // the folders are given when a computer connects: MTP starts again with them (nothing is arriving)
    if (MtpSnapshot().file.empty()) {
        MtpStop();
        OpenMoreScreen(MoreScreen::Mtp);
    }
    Notice(TrT().mu_note);
}

void App::UpdateArrival(u64 down, u64 held) {
    if (m_more == MoreScreen::Mtp) {
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_mtp_unit > 0) m_mtp_unit--;
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_mtp_unit < 3) m_mtp_unit++;
        if (down & HidNpadButton_A) {
            ToggleMtpUnit(m_mtp_unit);
            return;
        }
    }
    if (m_more == MoreScreen::Mtp && (down & HidNpadButton_X)) {
        if (MtpSnapshot().nand_allowed) {
            MtpAllowNand(false); // only the SD card again, at once
            return;
        }
        // the internal memory, only after its question (Cancel in focus)
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::MtpNand;
        return;
    }
    if (m_more == MoreScreen::Usb && (down & HidNpadButton_ZR)) m_files_nand = !m_files_nand;
    if (down & HidNpadButton_B) LeaveMoreScreen();
}

} // namespace nxdock
