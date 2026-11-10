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
#include "mtp.hpp"
#include "usb_disks.hpp"
#include "usb_tinfoil.hpp"

#include <algorithm>
#include <cmath>

namespace nxdock {
namespace {

constexpr int kTiles = 8;
constexpr float kTileGap = 18, kTileW = (kWidth - 96 - 3 * kTileGap) / 4, kTileH = 170;
constexpr float kSideW = 250;
constexpr float kRowTop = 134, kRowPitch = 50, kRowsBottom = 584;
constexpr int kVisibleRows = int((kRowsBottom - kRowTop) / kRowPitch);

std::string BaseName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

// ---- the tiles ----

void App::OpenMoreTile(int tile) {
    const auto& s = Tr();
    m_tile = tile;
    switch (tile) {
        case 0: OpenMoreScreen(MoreScreen::Files); break;
        case 1: OpenMoreScreen(MoreScreen::Mtp); break;
        case 2: OpenMoreScreen(MoreScreen::Usb); break;
        case 3: OpenShops(); break;
        case 6: OpenMods(); break;
        case 7: AskScreenOff(); break;
        case 5: OpenSpace(); break;
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
            const bool ok = MtpStart([this](InstallJob job) { return m_queue && m_queue->Add(std::move(job)); },
                                     [this] { return m_queue && m_queue->Busy(); });
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
    const char* titles[kTiles] = {m.t_files, m.t_mtp, m.t_usb, m.t_shops, m.t_saves, m.t_space, m.t_mods, m.t_screen};
    const char* subs[kTiles] = {m.t_files_sub, m.t_mtp_sub, m.t_usb_sub, m.t_shops_sub, m.t_saves_sub, m.t_space_sub, m.t_mods_sub, m.t_screen_sub};
    m_tile = std::clamp(m_tile, 0, kTiles - 1);
    for (int i = 0; i < kTiles; i++) {
        const int row = i / 4, col = i % 4;
        const float y0 = 100 + row * (kTileH + 64);
        if (col == 0) draw::Section(vg, t, 50, y0, row == 0 ? m.sect_noserver : m.sect_console);
        const float x = 48 + col * (kTileW + kTileGap), y = y0 + 20;
        draw::Panel(vg, t, x, y, kTileW, kTileH, 16);
        if (i == m_tile) ui::Focus(vg, t, x, y, kTileW, kTileH, 16, m_time);
        AddHit(x, y, kTileW, kTileH, HitKind::MoreTile, i);
        DrawTileIcon(vg, i, x + 22, y + 40);
        ui::Text(vg, x + 20, y + 84, 19, t.text, titles[i]);
        draw::Box(vg, x + 20, y + 104, kTileW - 40, 14, t.muted, subs[i]);
    }
    std::vector<ui::Hint> hints = {{ui::glyph::A, m.open, HidNpadButton_A}, {ui::glyph::Plus, Tr().exit, HidNpadButton_Plus}};
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

// The pictures of the tiles, drawn in the blue of work: an SD card, a screen, a plug, a shop, a
// safe, a pie, a wand and a moon.
void App::DrawTileIcon(NVGcontext* vg, int tile, float x, float cy) {
    const Theme& t = m_theme;
    const NVGcolor c = t.work;
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
            nvgFillColor(vg, t.panel);
            nvgFill(vg);
            break;
    }
}

void App::UpdateTiles(u64 down, u64 held) {
    if (Repeat(down, held, HidNpadButton_AnyLeft) && m_tile % 4 > 0) m_tile--;
    if (Repeat(down, held, HidNpadButton_AnyRight) && m_tile % 4 < 3) m_tile++;
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_tile >= 4) m_tile -= 4;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_tile < 4) m_tile += 4;
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
    m_entries = ListFolder(folder, root);
    m_marked.assign(m_entries.size(), 0);
    m_file_focus = 0;
    m_file_first = 0;
}

void App::InstallMarkedFiles() {
    const auto& s = Tr();
    const More& m = TrM();
    std::vector<InstallJob> jobs;
    bool any = std::any_of(m_marked.begin(), m_marked.end(), [](char c) { return c; });
    for (size_t i = 0; i < m_entries.size(); i++) {
        const auto& e = m_entries[i];
        const bool chosen = any ? m_marked[i] : int(i) == m_file_focus;
        if (!chosen || e.dir) continue;
        InstallJob job;
        job.game = e.name;
        job.label = e.parts.size() > 1 ? std::string(m.t_files) + " · " + std::to_string(e.parts.size()) : m.t_files;
        job.format = e.format;
        job.origin = InstallJob::Origin::File;
        job.parts = e.parts;
        job.file_id = "file:" + e.path;
        job.size = e.size;
        job.nand = m_files_nand;
        job.color_key = e.name;
        jobs.push_back(std::move(job));
    }
    if (jobs.empty()) {
        Notice(s.nothing_picked);
        return;
    }
    std::fill(m_marked.begin(), m_marked.end(), 0);
    QueueLocal(std::move(jobs));
}

// Jobs that do not come from the library: the internal memory asks first, as on a game's page.
void App::QueueLocal(std::vector<InstallJob> jobs) {
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
    for (int i = 0; i < int(m_place_roots.size()); i++) {
        const float y = 118 + i * 50;
        const bool on = i == m_place;
        if (on) {
            NVGcolor bg = t.work;
            bg.a = 0.16f;
            ui::FillRound(vg, 14, y, kSideW - 28, 42, 10, bg);
        }
        if (m_files_side && on) ui::Focus(vg, t, 14, y, kSideW - 28, 42, 10, m_time);
        AddHit(14, y, kSideW - 28, 42, HitKind::MorePlace, i);
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
    nvgSave(vg);
    nvgScissor(vg, lx, 86, lw, 30);
    ui::Text(vg, lx + 4, 104, 15, t.muted, m_folder.c_str());
    nvgRestore(vg);
    if (m_entries.empty()) {
        ui::Text(vg, lx + lw / 2, 300, 18, t.muted, m.empty_folder, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    m_file_focus = std::clamp(m_file_focus, 0, std::max(0, int(m_entries.size()) - 1));
    if (m_file_focus < m_file_first) m_file_first = m_file_focus;
    if (m_file_focus >= m_file_first + kVisibleRows) m_file_first = m_file_focus - kVisibleRows + 1;
    for (int r = 0; r < kVisibleRows && m_file_first + r < int(m_entries.size()); r++) {
        const int i = m_file_first + r;
        const auto& e = m_entries[i];
        const float y = kRowTop + r * kRowPitch, cy = y + 22;
        if (i == m_file_focus && !m_files_side) {
            ui::FillRound(vg, lx, y, lw, 44, 10, t.panel2);
            ui::Focus(vg, t, lx, y, lw, 44, 10, m_time);
        }
        AddHit(lx + 50, y, lw - 50, 44, HitKind::MoreRow, i);
        if (!e.dir) {
            draw::TickBox(vg, t, lx + 14, cy, m_marked[i]);
            AddHit(lx, y, 50, 44, HitKind::MoreMark, i);
        }
        if (e.dir) draw::FolderIcon(vg, t, lx + 54, cy);
        else draw::FileIcon(vg, t, lx + 54, cy);
        std::string name = e.name;
        if (e.parts.size() > 1) {
            char split[48];
            std::snprintf(split, sizeof(split), m.split_fmt, int(e.parts.size()));
            name += "  (" + std::string(split) + ")";
        }
        nvgSave(vg);
        nvgScissor(vg, lx + 90, y, lw - 90 - 220, 44);
        ui::Text(vg, lx + 90, cy, 16, t.text, name.c_str());
        nvgRestore(vg);
        if (!e.dir) {
            std::string fmt = e.format;
            for (auto& c : fmt) c = char(std::toupper((unsigned char)c));
            ui::Text(vg, lx + lw - 120, cy, 15, t.muted, fmt.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            ui::Text(vg, lx + lw - 14, cy, 15, t.muted, draw::Size(e.size).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
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
    const float sy = 596, sh = 52;
    draw::Panel(vg, t, lx, sy, lw, sh, 12);
    AddHit(lx, sy, lw, sh, HitKind::Button);
    m_hits.back().rect.button = HidNpadButton_R; // a tap on the strip changes where it goes
    char text[200];
    std::snprintf(text, sizeof(text), m.marked_fmt, count, draw::Size(bytes).c_str(), m_files_nand ? s.internal : s.sd_card);
    ui::CheckMark(vg, lx + 24, sy + sh / 2, 12, t.work, 2.4f);
    ui::Text(vg, lx + 44, sy + sh / 2, 16, t.text, text);
    const s64 free_bytes = m_files_nand ? m_nand_free_bytes : m_sd_free_bytes;
    if (free_bytes >= 0) {
        const unsigned long long left = u64(free_bytes) > bytes ? u64(free_bytes) - bytes : 0;
        std::snprintf(text, sizeof(text), m.left_after_fmt, draw::Size(left).c_str());
        ui::Text(vg, lx + lw - 18, sy + sh / 2, 15, u64(free_bytes) < bytes ? t.err : t.muted, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }

    std::vector<ui::Hint> hints = {{ui::glyph::Y, m.mark, HidNpadButton_Y},
                                   {ui::glyph::A, s.select, HidNpadButton_A},
                                   {ui::glyph::X, m.install_marked, HidNpadButton_X},
                                   {ui::glyph::R, m.sd_or_internal, HidNpadButton_R},
                                   {ui::glyph::B, s.back, HidNpadButton_B}};
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::UpdateFiles(u64 down, u64 held) {
    if (Repeat(down, held, HidNpadButton_AnyLeft)) m_files_side = true;
    if (Repeat(down, held, HidNpadButton_AnyRight)) m_files_side = false;
    if (down & HidNpadButton_R) m_files_nand = !m_files_nand;
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
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_file_focus > 0) m_file_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_file_focus < int(m_entries.size()) - 1) m_file_focus++;
    const bool has = m_file_focus >= 0 && m_file_focus < int(m_entries.size());
    if (has && (down & (HidNpadButton_A | HidNpadButton_Y))) {
        const auto e = m_entries[m_file_focus];
        if (e.dir) {
            if (down & HidNpadButton_A) {
                const bool to_root = e.name == ".." && std::find(m_place_roots.begin(), m_place_roots.end(), e.path) != m_place_roots.end();
                OpenFolder(e.path, to_root);
            }
        } else {
            m_marked[m_file_focus] = !m_marked[m_file_focus];
        }
    }
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
            ui::FillRound(vg, px + 26, y, pw - 52, 66, 12, t.panel2);
            ui::StrokeRound(vg, px + 26.5f, y + .5f, pw - 53, 65, 12, t.line, 1);
            nvgGlobalAlpha(vg, f.off ? 0.55f : 1.f);
            draw::FolderIcon(vg, t, px + 44, y + 33);
            ui::Text(vg, px + 82, y + 23, 17, t.text, f.name);
            ui::Text(vg, px + 82, y + 46, 14, t.muted, f.sub);
            nvgGlobalAlpha(vg, 1.f);
            y += 78;
        }
    } else {
        draw::Box(vg, px + 26, y, pw - 52, 17, t.muted, m.usb_steps);
    }
    if (!m_more_error.empty()) {
        draw::Box(vg, px + 26, py + ph - 70, pw - 52, 15, t.err, m_more_error.c_str());
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

    std::vector<ui::Hint> hints;
    if (mtp && !mv.nand_allowed) hints.push_back({ui::glyph::X, m.allow_nand, HidNpadButton_X});
    if (!mtp) hints.push_back({ui::glyph::R, m_files_nand ? s.internal : s.sd_card, HidNpadButton_R});
    hints.push_back({ui::glyph::B, m.stop_back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::UpdateArrival(u64 down, u64 /*held*/) {
    if (m_more == MoreScreen::Mtp && (down & HidNpadButton_X) && !MtpSnapshot().nand_allowed) {
        // the internal memory, only after its question (Cancel in focus)
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::MtpNand;
        return;
    }
    if (m_more == MoreScreen::Usb && (down & HidNpadButton_R)) m_files_nand = !m_files_nand;
    if (down & HidNpadButton_B) LeaveMoreScreen();
}

} // namespace nxdock
